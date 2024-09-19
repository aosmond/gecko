/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DecoderDoctorDiagnostics.h"

#include <string.h>
#include <unordered_map>

#include "VideoUtils.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/GlobalTeardownObserver.h"
#include "mozilla/Logging.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/Services.h"
#include "mozilla/ThreadLocal.h"
#include "mozilla/TimeStamp.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"
#include "nsGlobalWindowInner.h"
#include "nsIObserverService.h"
#include "nsIScriptError.h"
#include "nsITimer.h"
#include "nsPrintfCString.h"

#if defined(MOZ_FFMPEG)
#  include "FFmpegRuntimeLinker.h"
#endif

static mozilla::LazyLogModule sDecoderDoctorLog("DecoderDoctor");
#define DD_LOG(level, arg, ...) \
  MOZ_LOG(sDecoderDoctorLog, level, (arg, ##__VA_ARGS__))
#define DD_DEBUG(arg, ...) DD_LOG(mozilla::LogLevel::Debug, arg, ##__VA_ARGS__)
#define DD_INFO(arg, ...) DD_LOG(mozilla::LogLevel::Info, arg, ##__VA_ARGS__)
#define DD_WARN(arg, ...) DD_LOG(mozilla::LogLevel::Warning, arg, ##__VA_ARGS__)

namespace mozilla {

// Class that collects a sequence of diagnostics from the same document over a
// small period of time, in order to provide a synthesized analysis.
//
// Referenced by the document through a nsINode property, mTimer, and
// inter-task captures.
// When notified that the document is dead, or when the timer expires but
// nothing new happened, StopWatching() will remove the document property and
// timer (if present), so no more work will happen and the watcher will be
// destroyed once all references are gone.
class DecoderDoctorGlobalWatcher final : public GlobalTeardownObserver,
                                         public nsITimerCallback,
                                         public nsINamed {
 public:
  static already_AddRefed<DecoderDoctorGlobalWatcher> RetrieveOrCreate(
      nsIGlobalObject* aGlobal);

  NS_DECL_ISUPPORTS
  NS_DECL_NSITIMERCALLBACK
  NS_DECL_NSINAMED

  void AddDiagnostics(DecoderDoctorDiagnostics&& aDiagnostics,
                      const char* aCallSite);

  void DisconnectFromOwner() override;

 private:
  explicit DecoderDoctorGlobalWatcher(nsIGlobalObject* aGlobal);
  virtual ~DecoderDoctorGlobalWatcher();

  // This will prevent further work from happening, watcher will deregister
  // itself from document (if requested) and cancel any timer, and soon die.
  void StopWatching(bool aRemoveProperty);

  static const uint32_t sAnalysisPeriod_ms = 1000;
  void EnsureTimerIsStarted();

  void SynthesizeAnalysis();
  // This is used for testing and will generate an analysis based on the value
  // set in `media.decoder-doctor.testing.fake-error`.
  void SynthesizeFakeAnalysis();
  bool ShouldSynthesizeFakeAnalysis() const;

  struct Diagnostics {
    Diagnostics(DecoderDoctorDiagnostics&& aDiagnostics, const char* aCallSite,
                mozilla::TimeStamp aTimeStamp)
        : mDecoderDoctorDiagnostics(std::move(aDiagnostics)),
          mCallSite(aCallSite),
          mTimeStamp(aTimeStamp) {}
    Diagnostics(const Diagnostics&) = delete;
    Diagnostics(Diagnostics&& aOther)
        : mDecoderDoctorDiagnostics(
              std::move(aOther.mDecoderDoctorDiagnostics)),
          mCallSite(std::move(aOther.mCallSite)),
          mTimeStamp(aOther.mTimeStamp) {}

    const DecoderDoctorDiagnostics mDecoderDoctorDiagnostics;
    const nsCString mCallSite;
    const mozilla::TimeStamp mTimeStamp;
  };
  typedef nsTArray<Diagnostics> DiagnosticsSequence;
  DiagnosticsSequence mDiagnosticsSequence;

  nsCOMPtr<nsITimer> mTimer;  // Keep timer alive until we run.
  DiagnosticsSequence::size_type mDiagnosticsHandled = 0;

  static MOZ_THREAD_LOCAL(DecoderDoctorGlobalWatcher*) sWorkerWatcher;

  static std::unordered_map<nsIGlobalObject*, DecoderDoctorGlobalWatcher*>
      sMainWatchers;
};

NS_IMPL_ISUPPORTS(DecoderDoctorGlobalWatcher, nsITimerCallback, nsINamed)

MOZ_THREAD_LOCAL(DecoderDoctorGlobalWatcher*)
DecoderDoctorGlobalWatcher::sWorkerWatcher;

std::unordered_map<nsIGlobalObject*, DecoderDoctorGlobalWatcher*>
    DecoderDoctorGlobalWatcher::sMainWatchers;

// static
already_AddRefed<DecoderDoctorGlobalWatcher>
DecoderDoctorGlobalWatcher::RetrieveOrCreate(nsIGlobalObject* aGlobalObject) {
  MOZ_ASSERT(aGlobalObject);

  if (!aGlobalObject->IsDying()) {
    return nullptr;
  }

  if (dom::IsCurrentThreadRunningWorker()) {
    if (NS_WARN_IF(!sWorkerWatcher.init())) {
      return nullptr;
    }

    if (sWorkerWatcher.get()) {
      return do_AddRef(sWorkerWatcher.get());
    }

    RefPtr<DecoderDoctorGlobalWatcher> watcher =
        new DecoderDoctorGlobalWatcher(aGlobalObject);
    sWorkerWatcher.set(watcher.get());
    return watcher.forget();
  }

  if (NS_WARN_IF(!NS_IsMainThread())) {
    MOZ_ASSERT_UNREACHABLE("Cannot use off main or DOM worker threads!");
    return nullptr;
  }

  auto i = sMainWatchers.find(aGlobalObject);
  if (i != sMainWatchers.end()) {
    return do_AddRef(i->second);
  }

  RefPtr<DecoderDoctorGlobalWatcher> watcher =
      new DecoderDoctorGlobalWatcher(aGlobalObject);
  sMainWatchers[aGlobalObject] = watcher;
  return watcher.forget();
}

DecoderDoctorGlobalWatcher::DecoderDoctorGlobalWatcher(
    nsIGlobalObject* aGlobalObject)
    : GlobalTeardownObserver(aGlobalObject) {
  MOZ_ASSERT(GetOwnerGlobal());
  DD_DEBUG(
      "DecoderDoctorGlobalWatcher[%p]::DecoderDoctorGlobalWatcher(global=%p)",
      this, GetOwnerGlobal());
}

DecoderDoctorGlobalWatcher::~DecoderDoctorGlobalWatcher() {
  NS_ASSERT_OWNINGTHREAD(DecoderDoctorGlobalWatcher);
  DD_DEBUG(
      "DecoderDoctorGlobalWatcher[%p, global=%p <- expect "
      "0]::~DecoderDoctorGlobalWatcher()",
      this, GetOwnerGlobal());
  MOZ_ASSERT(!GetOwnerGlobal());
}

void DecoderDoctorGlobalWatcher::DisconnectFromOwner() {
  NS_ASSERT_OWNINGTHREAD(DecoderDoctorGlobalWatcher);
  DD_DEBUG("DecoderDoctorGlobalWatcher[%p, global=%p]::DisconnectFromOwner()\n",
           this, GetOwnerGlobal());

  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }

  if (NS_IsMainThread()) {
    if (auto* global = GetOwnerGlobal()) {
      sMainWatchers.erase(global);
    }
  } else if (sWorkerWatcher.init() && sWorkerWatcher.get() == this) {
    sWorkerWatcher.set(nullptr);
  }

  GlobalTeardownObserver::DisconnectFromOwner();
}

void DecoderDoctorGlobalWatcher::EnsureTimerIsStarted() {
  NS_ASSERT_OWNINGTHREAD(DecoderDoctorGlobalWatcher);

  if (!mTimer) {
    NS_NewTimerWithCallback(getter_AddRefs(mTimer), this, sAnalysisPeriod_ms,
                            nsITimer::TYPE_ONE_SHOT);
  }
}

enum class ReportParam : uint8_t {
  // Marks the end of the parameter list.
  // Keep this zero! (For implicit zero-inits when used in definitions below.)
  None = 0,

  Formats,
  DecodeIssue,
  DocURL,
  ResourceURL
};

struct NotificationAndReportStringId {
  // Notification type, handled by DecoderDoctorChild.sys.mjs and
  // DecoderDoctorParent.sys.mjs.
  dom::DecoderDoctorNotificationType mNotificationType;
  // Console message id. Key in dom/locales/.../chrome/dom/dom.properties.
  const char* mReportStringId;
  DataMutexString::ConstAutoLock (*mFormatsPrefFn)() = nullptr;
  static const int maxReportParams = 4;
  ReportParam mReportParams[maxReportParams];
};

// Note: ReportStringIds are limited to alphanumeric only.
static const NotificationAndReportStringId sMediaWidevineNoWMF = {
    dom::DecoderDoctorNotificationType::Platform_decoder_not_found,
    "MediaWidevineNoWMF",
    &StaticPrefs::media_decoder_doctor_WidevineNoWMF_formats,
    {ReportParam::None}};
static const NotificationAndReportStringId sMediaWMFNeeded = {
    dom::DecoderDoctorNotificationType::Platform_decoder_not_found,
    "MediaWMFNeeded",
    &StaticPrefs::media_decoder_doctor_WidevineWMFNeeded_formats,
    {ReportParam::Formats}};
static const NotificationAndReportStringId sMediaFFMpegNotFound = {
    dom::DecoderDoctorNotificationType::Platform_decoder_not_found,
    "MediaPlatformDecoderNotFound",
    &StaticPrefs::media_decoder_doctor_PlatformDecoderNotFound_formats,
    {ReportParam::Formats}};
static const NotificationAndReportStringId sMediaCannotPlayNoDecoders = {
    dom::DecoderDoctorNotificationType::Cannot_play,
    "MediaCannotPlayNoDecoders",
    &StaticPrefs::media_decoder_doctor_CannotPlayNoDecoders_formats,
    {ReportParam::Formats}};
static const NotificationAndReportStringId sMediaNoDecoders = {
    dom::DecoderDoctorNotificationType::Can_play_but_some_missing_decoders,
    "MediaNoDecoders",
    &StaticPrefs::media_decoder_doctor_NoDecoders_formats,
    {ReportParam::Formats}};
static const NotificationAndReportStringId sCannotInitializePulseAudio = {
    dom::DecoderDoctorNotificationType::Cannot_initialize_pulseaudio,
    "MediaCannotInitializePulseAudio",
    &StaticPrefs::media_decoder_doctor_CannotInitializePulseAudio_formats,
    {ReportParam::None}};
static const NotificationAndReportStringId sUnsupportedLibavcodec = {
    dom::DecoderDoctorNotificationType::Unsupported_libavcodec,
    "MediaUnsupportedLibavcodec",
    &StaticPrefs::media_decoder_doctor_UnsupportedLibavcodec_formats,
    {ReportParam::None}};
static const NotificationAndReportStringId sMediaDecodeError = {
    dom::DecoderDoctorNotificationType::Decode_error,
    "MediaDecodeError",
    &StaticPrefs::media_decoder_doctor_DecodeError_formats,
    {ReportParam::ResourceURL, ReportParam::DecodeIssue}};
static const NotificationAndReportStringId sMediaDecodeWarning = {
    dom::DecoderDoctorNotificationType::Decode_warning,
    "MediaDecodeWarning",
    &StaticPrefs::media_decoder_doctor_DecodeWarning_formats,
    {ReportParam::ResourceURL, ReportParam::DecodeIssue}};

static const NotificationAndReportStringId* const
    sAllNotificationsAndReportStringIds[] = {
        &sMediaWidevineNoWMF,    &sMediaWMFNeeded,
        &sMediaFFMpegNotFound,   &sMediaCannotPlayNoDecoders,
        &sMediaNoDecoders,       &sCannotInitializePulseAudio,
        &sUnsupportedLibavcodec, &sMediaDecodeError,
        &sMediaDecodeWarning};

// Create a webcompat-friendly description of a MediaResult.
static nsString MediaResultDescription(const MediaResult& aResult,
                                       bool aIsError) {
  nsCString name;
  GetErrorName(aResult.Code(), name);
  return NS_ConvertUTF8toUTF16(nsPrintfCString(
      "%s Code: %s (0x%08" PRIx32 ")%s%s", aIsError ? "Error" : "Warning",
      name.get(), static_cast<uint32_t>(aResult.Code()),
      aResult.Message().IsEmpty() ? "" : "\nDetails: ",
      aResult.Message().get()));
}

static bool IsNotificationAllowedOnPlatform(
    const NotificationAndReportStringId& aNotification) {
  // Allow all notifications during testing.
  if (StaticPrefs::media_decoder_doctor_testing()) {
    return true;
  }
  // These notifications are platform independent.
  if (aNotification.mNotificationType ==
          dom::DecoderDoctorNotificationType::Cannot_play ||
      aNotification.mNotificationType ==
          dom::DecoderDoctorNotificationType::
              Can_play_but_some_missing_decoders ||
      aNotification.mNotificationType ==
          dom::DecoderDoctorNotificationType::Decode_error ||
      aNotification.mNotificationType ==
          dom::DecoderDoctorNotificationType::Decode_warning) {
    return true;
  }
#if defined(XP_WIN)
  if (aNotification.mNotificationType ==
      dom::DecoderDoctorNotificationType::Platform_decoder_not_found) {
    return strcmp(sMediaWMFNeeded.mReportStringId,
                  aNotification.mReportStringId) == 0 ||
           strcmp(sMediaWidevineNoWMF.mReportStringId,
                  aNotification.mReportStringId) == 0;
  }
#endif
#if defined(MOZ_FFMPEG)
  if (aNotification.mNotificationType ==
      dom::DecoderDoctorNotificationType::Platform_decoder_not_found) {
    return strcmp(sMediaFFMpegNotFound.mReportStringId,
                  aNotification.mReportStringId) == 0;
  }
  if (aNotification.mNotificationType ==
      dom::DecoderDoctorNotificationType::Unsupported_libavcodec) {
    return strcmp(sUnsupportedLibavcodec.mReportStringId,
                  aNotification.mReportStringId) == 0;
  }
#endif
#ifdef MOZ_PULSEAUDIO
  if (aNotification.mNotificationType ==
      dom::DecoderDoctorNotificationType::Cannot_initialize_pulseaudio) {
    return strcmp(sCannotInitializePulseAudio.mReportStringId,
                  aNotification.mReportStringId) == 0;
  }
#endif
  return false;
}

static void DispatchNotifyObservers(nsPIDOMWindowInner* aWindow,
                                    const nsAutoString& aData) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(aWindow, "decoder-doctor-notification", aData.get());
  }
}

static void DispatchNotification(
    nsIGlobalObject* aGlobalObject,
    const NotificationAndReportStringId& aNotification, bool aIsSolved,
    const nsAString& aFormats, const nsAString& aDecodeIssue,
    const nsACString& aDocURL, const nsAString& aResourceURL) {
  if (!aGlobalObject) {
    return;
  }
  dom::DecoderDoctorNotification data;
  data.mType = aNotification.mNotificationType;
  data.mIsSolved = aIsSolved;
  data.mDecoderDoctorReportId.Assign(
      NS_ConvertUTF8toUTF16(aNotification.mReportStringId));
  if (!aFormats.IsEmpty()) {
    data.mFormats.Construct(aFormats);
  }
  if (!aDecodeIssue.IsEmpty()) {
    data.mDecodeIssue.Construct(aDecodeIssue);
  }
  if (!aDocURL.IsEmpty()) {
    data.mDocURL.Construct(NS_ConvertUTF8toUTF16(aDocURL));
  }
  if (!aResourceURL.IsEmpty()) {
    data.mResourceURL.Construct(aResourceURL);
  }
  nsAutoString json;
  data.ToJSON(json);
  if (json.IsEmpty()) {
    DD_WARN(
        "DecoderDoctorDiagnostics/DispatchEvent() - Could not create json for "
        "dispatch");
    // No point in dispatching this notification without data, the front-end
    // wouldn't know what to display.
    return;
  }
  DD_DEBUG("DecoderDoctorDiagnostics/DispatchEvent() %s",
           NS_ConvertUTF16toUTF8(json).get());

  if (NS_IsMainThread()) {
    DispatchNotifyObservers(aGlobalObject->GetAsInnerWindow(), json);
    return;
  }

  if (auto* workerPrivate = dom::GetCurrentThreadWorkerPrivate()) {
    uint64_t windowID = workerPrivate->WindowID();
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "DecoderDoctorDiagnostics::DispatchNotification", [windowID, json]() {
          if (auto* window =
                  nsGlobalWindowInner::GetInnerWindowWithId(windowID)) {
            DispatchNotifyObservers(window, json);
          }
        }));
  }
}

static void ReportToConsole(dom::Document* aDocument,
                            const char* aConsoleStringId,
                            const nsTArray<nsString>& aParams) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!aDocument) {
    return;
  }

  if (StaticPrefs::media_decoder_doctor_testing()) {
    Unused << nsContentUtils::DispatchTrustedEvent(
        aDocument, aDocument, u"mozreportmediaerror"_ns, CanBubble::eNo,
        Cancelable::eNo);
  }

  nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "Media"_ns,
                                  aDocument, nsContentUtils::eDOM_PROPERTIES,
                                  aConsoleStringId, aParams);
}

static void ReportToConsole(nsIGlobalObject* aGlobalObject,
                            const char* aConsoleStringId,
                            nsTArray<nsString>&& aParams) {
  MOZ_ASSERT(aGlobalObject);

  DD_DEBUG(
      "DecoderDoctorDiagnostics.cpp:ReportToConsole(global=%p) ReportToConsole"
      " - aMsg='%s' params={%s%s%s%s}",
      aGlobalObject, aConsoleStringId,
      aParams.IsEmpty() ? "<no params>"
                        : NS_ConvertUTF16toUTF8(aParams[0]).get(),
      (aParams.Length() < 1 || aParams[0].IsEmpty()) ? "" : ", ",
      (aParams.Length() < 1 || aParams[0].IsEmpty())
          ? ""
          : NS_ConvertUTF16toUTF8(aParams[0]).get(),
      aParams.Length() < 2 ? "" : ", ...");

  if (NS_IsMainThread()) {
    if (auto* window = aGlobalObject->GetAsInnerWindow()) {
      ReportToConsole(window->GetExtantDoc(), aConsoleStringId, aParams);
    }
    return;
  }

  if (auto* workerPrivate = dom::GetCurrentThreadWorkerPrivate()) {
    uint64_t windowID = workerPrivate->WindowID();
    nsCString consoleStringId(aConsoleStringId);
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "DecoderDoctorDiagnostics::ReportToConsole",
        [windowID, consoleStringId = std::move(consoleStringId),
         params = std::move(aParams)]() {
          if (auto* window =
                  nsGlobalWindowInner::GetInnerWindowWithId(windowID)) {
            ReportToConsole(window->GetExtantDoc(), consoleStringId.Data(),
                            params);
          }
        }));
  }
}

static bool AllowNotification(
    const NotificationAndReportStringId& aNotification) {
  // "media.decoder-doctor.notifications-allowed" controls which notifications
  // may be dispatched to the front-end. It either contains:
  // - '*' -> Allow everything.
  // - Comma-separater list of ids -> Allow if aReportStringId (from
  //                                  dom.properties) is one of them.
  // - Nothing (missing or empty) -> Disable everything.
  const auto prefLock =
      StaticPrefs::media_decoder_doctor_notifications_allowed();
  return prefLock->EqualsLiteral("*") ||
         StringListContains(*prefLock, aNotification.mReportStringId);
}

static bool AllowDecodeIssue(const MediaResult& aDecodeIssue,
                             bool aDecodeIssueIsError) {
  if (aDecodeIssue == NS_OK) {
    // 'NS_OK' means we are not actually reporting a decode issue, so we
    // allow the report.
    return true;
  }

  // "media.decoder-doctor.decode-{errors,warnings}-allowed" controls which
  // decode issues may be dispatched to the front-end. It either contains:
  // - '*' -> Allow everything.
  // - Comma-separater list of ids -> Allow if the issue name is one of them.
  // - Nothing (missing or empty) -> Disable everything.
  nsAutoCString filter;
  {
    const auto prefLock =
        aDecodeIssueIsError
            ? StaticPrefs::media_decoder_doctor_decode_errors_allowed()
            : StaticPrefs::media_decoder_doctor_decode_warnings_allowed();
    if (prefLock->EqualsLiteral("*")) {
      return true;
    }

    filter.Assign(*prefLock);
  }

  nsCString decodeIssueName;
  GetErrorName(aDecodeIssue.Code(), static_cast<nsACString&>(decodeIssueName));
  return StringListContains(filter, decodeIssueName);
}

static void ReportAnalysis(nsIGlobalObject* aGlobalObject,
                           const NotificationAndReportStringId& aNotification,
                           bool aIsSolved, const nsAString& aFormats = u""_ns,
                           const MediaResult& aDecodeIssue = NS_OK,
                           bool aDecodeIssueIsError = true,
                           const nsACString& aDocURL = ""_ns,
                           const nsAString& aResourceURL = u""_ns) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!aGlobalObject) {
    return;
  }

  // Some errors should only appear on the specific platform. Eg. WMF related
  // error only happens on Windows.
  if (!IsNotificationAllowedOnPlatform(aNotification)) {
    DD_WARN("Platform doesn't support '%s'!", aNotification.mReportStringId);
    return;
  }

  nsString decodeIssueDescription;
  if (aDecodeIssue != NS_OK) {
    decodeIssueDescription.Assign(
        MediaResultDescription(aDecodeIssue, aDecodeIssueIsError));
  }

  // Report non-solved issues to console.
  if (!aIsSolved) {
    // Build parameter array needed by console message.
    AutoTArray<nsString, NotificationAndReportStringId::maxReportParams> params;
    for (int i = 0; i < NotificationAndReportStringId::maxReportParams; ++i) {
      if (aNotification.mReportParams[i] == ReportParam::None) {
        break;
      }
      switch (aNotification.mReportParams[i]) {
        case ReportParam::Formats:
          params.AppendElement(aFormats);
          break;
        case ReportParam::DecodeIssue:
          params.AppendElement(decodeIssueDescription);
          break;
        case ReportParam::DocURL:
          params.AppendElement(NS_ConvertUTF8toUTF16(aDocURL));
          break;
        case ReportParam::ResourceURL:
          params.AppendElement(aResourceURL);
          break;
        default:
          MOZ_ASSERT_UNREACHABLE("Bad notification parameter choice");
          break;
      }
    }
    ReportToConsole(aGlobalObject, aNotification.mReportStringId,
                    std::move(params));
  }

  const bool allowNotification = AllowNotification(aNotification);
  const bool allowDecodeIssue =
      AllowDecodeIssue(aDecodeIssue, aDecodeIssueIsError);
  DD_INFO(
      "ReportAnalysis for %s (decodeResult=%s) [AllowNotification=%d, "
      "AllowDecodeIssue=%d]",
      aNotification.mReportStringId, aDecodeIssue.ErrorName().get(),
      allowNotification, allowDecodeIssue);
  if (allowNotification && allowDecodeIssue) {
    DispatchNotification(aGlobalObject, aNotification, aIsSolved, aFormats,
                         decodeIssueDescription, aDocURL, aResourceURL);
  }
}

static nsString CleanItemForFormatsList(const nsAString& aItem) {
  nsString item(aItem);
  // Remove commas from item, as commas are used to separate items. It's fine
  // to have a one-way mapping, it's only used for comparisons and in
  // console display (where formats shouldn't contain commas in the first place)
  item.ReplaceChar(',', ' ');
  item.CompressWhitespace();
  return item;
}

static void AppendToFormatsList(nsAString& aList, const nsAString& aItem) {
  if (!aList.IsEmpty()) {
    aList += u", "_ns;
  }
  aList += CleanItemForFormatsList(aItem);
}

static bool FormatsListContains(const nsAString& aList,
                                const nsAString& aItem) {
  return StringListContains(aList, CleanItemForFormatsList(aItem));
}

static const char* GetLinkStatusLibraryName() {
#if defined(MOZ_FFMPEG)
  return FFmpegRuntimeLinker::LinkStatusLibraryName();
#else
  return "no library (ffmpeg disabled during build)";
#endif
}

static const char* GetLinkStatusString() {
#if defined(MOZ_FFMPEG)
  return FFmpegRuntimeLinker::LinkStatusString();
#else
  return "no link (ffmpeg disabled during build)";
#endif
}

void DecoderDoctorGlobalWatcher::SynthesizeFakeAnalysis() {
  NS_ASSERT_OWNINGTHREAD(DecoderDoctorGlobalWatcher);

  nsAutoCString errorType;
  {
    const auto prefLock =
        StaticPrefs::media_decoder_doctor_testing_fake_error();
    errorType.Assign(*prefLock);
  }

  MOZ_ASSERT(!errorType.IsEmpty());
  for (const auto& id : sAllNotificationsAndReportStringIds) {
    if (strcmp(id->mReportStringId, errorType.get()) == 0) {
      if (id->mNotificationType ==
          dom::DecoderDoctorNotificationType::Decode_error) {
        ReportAnalysis(GetOwnerGlobal(), *id, false /* isSolved */, u"*"_ns,
                       NS_ERROR_DOM_MEDIA_DECODE_ERR, true /* IsDecodeError */);
      } else {
        ReportAnalysis(GetOwnerGlobal(), *id, false /* isSolved */, u"*"_ns,
                       NS_OK, false /* IsDecodeError */);
      }
      return;
    }
  }
}

void DecoderDoctorGlobalWatcher::SynthesizeAnalysis() {
  NS_ASSERT_OWNINGTHREAD(DecoderDoctorGlobalWatcher);

  nsAutoString playableFormats;
  nsAutoString unplayableFormats;
  // Subsets of unplayableFormats that require a specific platform decoder:
  nsAutoString formatsRequiringWMF;
  nsAutoString formatsRequiringFFMpeg;
  nsAutoString formatsLibAVCodecUnsupported;
  nsAutoString supportedKeySystems;
  nsAutoString unsupportedKeySystems;
  DecoderDoctorDiagnostics::KeySystemIssue lastKeySystemIssue =
      DecoderDoctorDiagnostics::eUnset;
  // Only deal with one decode error per document (the first one found).
  const MediaResult* firstDecodeError = nullptr;
  const nsString* firstDecodeErrorMediaSrc = nullptr;
  // Only deal with one decode warning per document (the first one found).
  const MediaResult* firstDecodeWarning = nullptr;
  const nsString* firstDecodeWarningMediaSrc = nullptr;

  for (const auto& diag : mDiagnosticsSequence) {
    switch (diag.mDecoderDoctorDiagnostics.Type()) {
      case DecoderDoctorDiagnostics::eFormatSupportCheck:
        if (diag.mDecoderDoctorDiagnostics.CanPlay()) {
          AppendToFormatsList(playableFormats,
                              diag.mDecoderDoctorDiagnostics.Format());
        } else {
          AppendToFormatsList(unplayableFormats,
                              diag.mDecoderDoctorDiagnostics.Format());
          if (diag.mDecoderDoctorDiagnostics.DidWMFFailToLoad()) {
            AppendToFormatsList(formatsRequiringWMF,
                                diag.mDecoderDoctorDiagnostics.Format());
          } else if (diag.mDecoderDoctorDiagnostics.DidFFmpegNotFound()) {
            AppendToFormatsList(formatsRequiringFFMpeg,
                                diag.mDecoderDoctorDiagnostics.Format());
          } else if (diag.mDecoderDoctorDiagnostics.IsLibAVCodecUnsupported()) {
            AppendToFormatsList(formatsLibAVCodecUnsupported,
                                diag.mDecoderDoctorDiagnostics.Format());
          }
        }
        break;
      case DecoderDoctorDiagnostics::eMediaKeySystemAccessRequest:
        if (diag.mDecoderDoctorDiagnostics.IsKeySystemSupported()) {
          AppendToFormatsList(supportedKeySystems,
                              diag.mDecoderDoctorDiagnostics.KeySystem());
        } else {
          AppendToFormatsList(unsupportedKeySystems,
                              diag.mDecoderDoctorDiagnostics.KeySystem());
          DecoderDoctorDiagnostics::KeySystemIssue issue =
              diag.mDecoderDoctorDiagnostics.GetKeySystemIssue();
          if (issue != DecoderDoctorDiagnostics::eUnset) {
            lastKeySystemIssue = issue;
          }
        }
        break;
      case DecoderDoctorDiagnostics::eEvent:
        MOZ_ASSERT_UNREACHABLE("Events shouldn't be stored for processing.");
        break;
      case DecoderDoctorDiagnostics::eDecodeError:
        if (!firstDecodeError) {
          firstDecodeError = &diag.mDecoderDoctorDiagnostics.DecodeIssue();
          firstDecodeErrorMediaSrc =
              &diag.mDecoderDoctorDiagnostics.DecodeIssueMediaSrc();
        }
        break;
      case DecoderDoctorDiagnostics::eDecodeWarning:
        if (!firstDecodeWarning) {
          firstDecodeWarning = &diag.mDecoderDoctorDiagnostics.DecodeIssue();
          firstDecodeWarningMediaSrc =
              &diag.mDecoderDoctorDiagnostics.DecodeIssueMediaSrc();
        }
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Unhandled DecoderDoctorDiagnostics type");
        break;
    }
  }

  // Check if issues have been solved, by finding if some now-playable
  // key systems or formats were previously recorded as having issues.
  if (!supportedKeySystems.IsEmpty() || !playableFormats.IsEmpty()) {
    DD_DEBUG(
        "DecoderDoctorGlobalWatcher[%p, global=%p]::SynthesizeAnalysis() - "
        "supported key systems '%s', playable formats '%s'; See if they show "
        "issues have been solved...",
        this, GetOwnerGlobal(),
        NS_ConvertUTF16toUTF8(supportedKeySystems).Data(),
        NS_ConvertUTF16toUTF8(playableFormats).get());
    const nsAString* workingFormatsArray[] = {&supportedKeySystems,
                                              &playableFormats};
    // For each type of notification, retrieve the pref that contains formats/
    // key systems with issues.
    for (const NotificationAndReportStringId* id :
         sAllNotificationsAndReportStringIds) {
      if (!id->mFormatsPrefFn) {
        continue;
      }

      nsAutoString formatsWithIssues;
      {
        const auto prefLock = id->mFormatsPrefFn();
        formatsWithIssues.Assign(NS_ConvertUTF8toUTF16(*prefLock));
      }

      if (formatsWithIssues.IsEmpty()) {
        continue;
      }
      // See if that list of formats-with-issues contains any formats that are
      // now playable/supported.
      bool solved = false;
      for (const nsAString* workingFormats : workingFormatsArray) {
        for (const auto& workingFormat : MakeStringListRange(*workingFormats)) {
          if (FormatsListContains(formatsWithIssues, workingFormat)) {
            // This now-working format used not to work -> Report solved issue.
            DD_INFO(
                "DecoderDoctorGlobalWatcher[%p, "
                "global=%p]::SynthesizeAnalysis() - %s solved ('%s' now works, "
                "it was in pref='%s')",
                this, GetOwnerGlobal(), id->mReportStringId,
                NS_ConvertUTF16toUTF8(workingFormat).get(),
                NS_ConvertUTF16toUTF8(formatsWithIssues).get());
            ReportAnalysis(GetOwnerGlobal(), *id, true, workingFormat);
            // This particular Notification&ReportId has been solved, no need
            // to keep looking at other keysys/formats that might solve it too.
            solved = true;
            break;
          }
        }
        if (solved) {
          break;
        }
      }
      if (!solved) {
        DD_DEBUG(
            "DecoderDoctorGlobalWatcher[%p, global=%p]::SynthesizeAnalysis() - "
            "%s not solved (pref='%s')",
            this, GetOwnerGlobal(), id->mReportStringId,
            NS_ConvertUTF16toUTF8(formatsWithIssues).get());
      }
    }
  }

  // Look at Key System issues first, as they take precedence over format
  // checks.
  if (!unsupportedKeySystems.IsEmpty() && supportedKeySystems.IsEmpty()) {
    // No supported key systems!
    switch (lastKeySystemIssue) {
      case DecoderDoctorDiagnostics::eWidevineWithNoWMF:
        DD_INFO(
            "DecoderDoctorGlobalWatcher[%p, global=%p]::SynthesizeAnalysis() - "
            "unsupported key systems: %s, Widevine without WMF",
            this, GetOwnerGlobal(),
            NS_ConvertUTF16toUTF8(unsupportedKeySystems).get());
        ReportAnalysis(GetOwnerGlobal(), sMediaWidevineNoWMF, false,
                       unsupportedKeySystems);
        return;
      default:
        break;
    }
  }

  // Next, check playability of requested formats.
  if (!unplayableFormats.IsEmpty()) {
    // Some requested formats cannot be played.
    if (playableFormats.IsEmpty()) {
      // No requested formats can be played. See if we can help the user, by
      // going through expected decoders from most to least desirable.
      if (!formatsRequiringWMF.IsEmpty()) {
        DD_INFO(
            "DecoderDoctorGlobalWatcher[%p, global=%p]::SynthesizeAnalysis() - "
            "unplayable formats: %s -> Cannot play media because WMF was not "
            "found",
            this, GetOwnerGlobal(),
            NS_ConvertUTF16toUTF8(formatsRequiringWMF).get());
        ReportAnalysis(GetOwnerGlobal(), sMediaWMFNeeded, false,
                       formatsRequiringWMF);
        return;
      }
      if (!formatsRequiringFFMpeg.IsEmpty()) {
        MOZ_DIAGNOSTIC_ASSERT(formatsLibAVCodecUnsupported.IsEmpty());
        DD_INFO(
            "DecoderDoctorGlobalWatcher[%p, "
            "global=%p]::SynthesizeAnalysis() - unplayable formats: %s -> "
            "Cannot play media because ffmpeg was not found (Reason: %s)",
            this, GetOwnerGlobal(),
            NS_ConvertUTF16toUTF8(formatsRequiringFFMpeg).get(),
            GetLinkStatusString());
        ReportAnalysis(GetOwnerGlobal(), sMediaFFMpegNotFound, false,
                       formatsRequiringFFMpeg);
        return;
      }
      if (!formatsLibAVCodecUnsupported.IsEmpty()) {
        MOZ_DIAGNOSTIC_ASSERT(formatsRequiringFFMpeg.IsEmpty());
        DD_INFO(
            "DecoderDoctorGlobalWatcher[%p, "
            "global=%p]::SynthesizeAnalysis() - unplayable formats: %s -> "
            "Cannot play media because of unsupported %s (Reason: %s)",
            this, GetOwnerGlobal(),
            NS_ConvertUTF16toUTF8(formatsLibAVCodecUnsupported).get(),
            GetLinkStatusLibraryName(), GetLinkStatusString());
        ReportAnalysis(GetOwnerGlobal(), sUnsupportedLibavcodec, false,
                       formatsLibAVCodecUnsupported);
        return;
      }
      DD_INFO(
          "DecoderDoctorGlobalWatcher[%p, global=%p]::SynthesizeAnalysis() - "
          "Cannot play media, unplayable formats: %s",
          this, GetOwnerGlobal(),
          NS_ConvertUTF16toUTF8(unplayableFormats).get());
      ReportAnalysis(GetOwnerGlobal(), sMediaCannotPlayNoDecoders, false,
                     unplayableFormats);
      return;
    }

    DD_INFO(
        "DecoderDoctorGlobalWatcher[%p, global=%p]::SynthesizeAnalysis() - Can "
        "play media, but no decoders for some requested formats: %s",
        this, GetOwnerGlobal(), NS_ConvertUTF16toUTF8(unplayableFormats).get());
    if (StaticPrefs::media_decoder_doctor_verbose()) {
      ReportAnalysis(GetOwnerGlobal(), sMediaNoDecoders, false,
                     unplayableFormats);
    }
    return;
  }

  if (firstDecodeError) {
    DD_INFO(
        "DecoderDoctorGlobalWatcher[%p, global=%p]::SynthesizeAnalysis() - "
        "Decode error: %s",
        this, GetOwnerGlobal(), firstDecodeError->Description().get());
    auto clientInfo = GetOwnerGlobal()->GetClientInfo();
    ReportAnalysis(
        GetOwnerGlobal(), sMediaDecodeError, false, u""_ns, *firstDecodeError,
        true,  // aDecodeIssueIsError=true
        clientInfo ? clientInfo->URL() : ""_ns, *firstDecodeErrorMediaSrc);
    return;
  }

  if (firstDecodeWarning) {
    DD_INFO(
        "DecoderDoctorGlobalWatcher[%p, global=%p]::SynthesizeAnalysis() - "
        "Decode warning: %s",
        this, GetOwnerGlobal(), firstDecodeWarning->Description().get());
    auto clientInfo = GetOwnerGlobal()->GetClientInfo();
    ReportAnalysis(GetOwnerGlobal(), sMediaDecodeWarning, false, u""_ns,
                   *firstDecodeWarning,
                   false,  // aDecodeIssueIsError=false
                   clientInfo ? clientInfo->URL() : ""_ns,
                   *firstDecodeWarningMediaSrc);
    return;
  }

  DD_DEBUG(
      "DecoderDoctorGlobalWatcher[%p, global=%p]::SynthesizeAnalysis() - Can "
      "play media, decoders available for all requested formats",
      this, GetOwnerGlobal());
}

void DecoderDoctorGlobalWatcher::AddDiagnostics(
    DecoderDoctorDiagnostics&& aDiagnostics, const char* aCallSite) {
  NS_ASSERT_OWNINGTHREAD(DecoderDoctorGlobalWatcher);
  MOZ_ASSERT(aDiagnostics.Type() != DecoderDoctorDiagnostics::eEvent);

  if (!GetOwnerGlobal()) {
    return;
  }

  const mozilla::TimeStamp now = mozilla::TimeStamp::Now();

  constexpr size_t MaxDiagnostics = 128;
  constexpr double MaxSeconds = 10.0;
  while (
      mDiagnosticsSequence.Length() > MaxDiagnostics ||
      (!mDiagnosticsSequence.IsEmpty() &&
       (now - mDiagnosticsSequence[0].mTimeStamp).ToSeconds() > MaxSeconds)) {
    // Too many, or too old.
    mDiagnosticsSequence.RemoveElementAt(0);
    if (mDiagnosticsHandled != 0) {
      // Make sure Notify picks up the new element added below.
      --mDiagnosticsHandled;
    }
  }

  DD_DEBUG(
      "DecoderDoctorGlobalWatcher[%p, "
      "global=%p]::AddDiagnostics(DecoderDoctorDiagnostics{%s}, call site "
      "'%s')",
      this, GetOwnerGlobal(), aDiagnostics.GetDescription().Data(), aCallSite);
  mDiagnosticsSequence.AppendElement(
      Diagnostics(std::move(aDiagnostics), aCallSite, now));
  EnsureTimerIsStarted();
}

bool DecoderDoctorGlobalWatcher::ShouldSynthesizeFakeAnalysis() const {
  if (!StaticPrefs::media_decoder_doctor_testing()) {
    return false;
  }
  const auto prefLock = StaticPrefs::media_decoder_doctor_testing_fake_error();
  return prefLock->IsEmpty();
}

NS_IMETHODIMP
DecoderDoctorGlobalWatcher::Notify(nsITimer* timer) {
  NS_ASSERT_OWNINGTHREAD(DecoderDoctorGlobalWatcher);
  MOZ_ASSERT(timer == mTimer);

  // Forget timer. (Assuming timer keeps itself and us alive during this call.)
  mTimer = nullptr;

  if (!GetOwnerGlobal()) {
    return NS_OK;
  }

  if (mDiagnosticsSequence.Length() > mDiagnosticsHandled) {
    // We have new diagnostic data.
    mDiagnosticsHandled = mDiagnosticsSequence.Length();

    if (ShouldSynthesizeFakeAnalysis()) {
      SynthesizeFakeAnalysis();
    } else {
      SynthesizeAnalysis();
    }

    // Restart timer, to redo analysis or stop watching this document,
    // depending on whether anything new happens.
    EnsureTimerIsStarted();
  } else {
    DD_DEBUG(
        "DecoderDoctorGlobalWatcher[%p, global=%p]::Notify() - No new "
        "diagnostics to analyze -> Stop watching",
        this, GetOwnerGlobal());
    // Stop watching this global, we don't expect more diagnostics for now.
    // If more diagnostics come in, we'll treat them as another burst,
    // separately.
    DisconnectFromOwner();
  }

  return NS_OK;
}

NS_IMETHODIMP
DecoderDoctorGlobalWatcher::GetName(nsACString& aName) {
  aName.AssignLiteral("DecoderDoctorGlobalWatcher_timer");
  return NS_OK;
}

void DecoderDoctorDiagnostics::StoreFormatDiagnostics(
    nsIGlobalObject* aGlobalObject, const nsAString& aFormat, bool aCanPlay,
    const char* aCallSite) {
  // Make sure we only store once.
  MOZ_ASSERT(mDiagnosticsType == eUnsaved);
  mDiagnosticsType = eFormatSupportCheck;

  if (NS_WARN_IF(aFormat.Length() > 2048)) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreFormatDiagnostics(nsIGlobalObject* "
        "aGlobalObject=%p, format= TOO LONG! '%s', can-play=%d, call site "
        "'%s')",
        aGlobalObject, this, NS_ConvertUTF16toUTF8(aFormat).get(), aCanPlay,
        aCallSite);
    return;
  }

  if (NS_WARN_IF(!aGlobalObject)) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreFormatDiagnostics(nsIGlobalObject* "
        "aGlobalObject=nullptr, format='%s', can-play=%d, call site '%s')",
        this, NS_ConvertUTF16toUTF8(aFormat).get(), aCanPlay, aCallSite);
    return;
  }
  if (NS_WARN_IF(aFormat.IsEmpty())) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreFormatDiagnostics(nsIGlobalObject* "
        "aGlobalObject=%p, format=<empty>, can-play=%d, call site '%s')",
        this, aGlobalObject, aCanPlay, aCallSite);
    return;
  }

  RefPtr<DecoderDoctorGlobalWatcher> watcher =
      DecoderDoctorGlobalWatcher::RetrieveOrCreate(aGlobalObject);

  if (NS_WARN_IF(!watcher)) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreFormatDiagnostics(nsIGlobalObject* "
        "aGlobalObject=%p, format='%s', can-play=%d, call site '%s') - Could "
        "not "
        "create document watcher",
        this, aGlobalObject, NS_ConvertUTF16toUTF8(aFormat).get(), aCanPlay,
        aCallSite);
    return;
  }

  mFormat = aFormat;
  if (aCanPlay) {
    mFlags += Flags::CanPlay;
  } else {
    mFlags -= Flags::CanPlay;
  }

  // StoreDiagnostics should only be called once, after all data is available,
  // so it is safe to std::move() from this object.
  watcher->AddDiagnostics(std::move(*this), aCallSite);
  // Even though it's moved-from, the type should stay set
  // (Only used to ensure that we do store only once.)
  MOZ_ASSERT(mDiagnosticsType == eFormatSupportCheck);
}

void DecoderDoctorDiagnostics::StoreMediaKeySystemAccess(
    nsIGlobalObject* aGlobalObject, const nsAString& aKeySystem,
    bool aIsSupported, const char* aCallSite) {
  // Make sure we only store once.
  MOZ_ASSERT(mDiagnosticsType == eUnsaved);
  mDiagnosticsType = eMediaKeySystemAccessRequest;

  if (NS_WARN_IF(aKeySystem.Length() > 2048)) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreMediaKeySystemAccess("
        "nsIGlobalObject* "
        "aGlobalObject=%p, keysystem= TOO LONG! '%s', supported=%d, call site "
        "'%s')",
        aGlobalObject, this, NS_ConvertUTF16toUTF8(aKeySystem).get(),
        aIsSupported, aCallSite);
    return;
  }

  if (NS_WARN_IF(!aGlobalObject)) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreMediaKeySystemAccess("
        "nsIGlobalObject* "
        "aGlobalObject=nullptr, keysystem='%s', supported=%d, call site '%s')",
        this, NS_ConvertUTF16toUTF8(aKeySystem).get(), aIsSupported, aCallSite);
    return;
  }
  if (NS_WARN_IF(aKeySystem.IsEmpty())) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreMediaKeySystemAccess("
        "nsIGlobalObject* "
        "aGlobalObject=%p, keysystem=<empty>, supported=%d, call site '%s')",
        this, aGlobalObject, aIsSupported, aCallSite);
    return;
  }

  RefPtr<DecoderDoctorGlobalWatcher> watcher =
      DecoderDoctorGlobalWatcher::RetrieveOrCreate(aGlobalObject);

  if (NS_WARN_IF(!watcher)) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreMediaKeySystemAccess("
        "nsIGlobalObject* "
        "aGlobalObject=%p, keysystem='%s', supported=%d, call site '%s') - "
        "Could "
        "not create document watcher",
        this, aGlobalObject, NS_ConvertUTF16toUTF8(aKeySystem).get(),
        aIsSupported, aCallSite);
    return;
  }

  mKeySystem = aKeySystem;
  mIsKeySystemSupported = aIsSupported;

  // StoreMediaKeySystemAccess should only be called once, after all data is
  // available, so it is safe to std::move() from this object.
  watcher->AddDiagnostics(std::move(*this), aCallSite);
  // Even though it's moved-from, the type should stay set
  // (Only used to ensure that we do store only once.)
  MOZ_ASSERT(mDiagnosticsType == eMediaKeySystemAccessRequest);
}

void DecoderDoctorDiagnostics::StoreEvent(nsIGlobalObject* aGlobalObject,
                                          const DecoderDoctorEvent& aEvent,
                                          const char* aCallSite) {
  // Make sure we only store once.
  MOZ_ASSERT(mDiagnosticsType == eUnsaved);
  mDiagnosticsType = eEvent;
  mEvent = aEvent;

  if (NS_WARN_IF(!aGlobalObject)) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreEvent(nsIGlobalObject* "
        "aGlobalObject=nullptr, aEvent=%s, call site '%s')",
        this, GetDescription().get(), aCallSite);
    return;
  }

  // Don't keep events for later processing, just handle them now.
  switch (aEvent.mDomain) {
    case DecoderDoctorEvent::eAudioSinkStartup:
      if (aEvent.mResult == NS_ERROR_DOM_MEDIA_CUBEB_INITIALIZATION_ERR) {
        DD_INFO(
            "DecoderDoctorGlobalWatcher[%p, global=%p]::AddDiagnostics() - "
            "unable to initialize PulseAudio",
            this, aGlobalObject);
        ReportAnalysis(aGlobalObject, sCannotInitializePulseAudio, false,
                       u"*"_ns);
      } else if (aEvent.mResult == NS_OK) {
        DD_INFO(
            "DecoderDoctorGlobalWatcher[%p, global=%p]::AddDiagnostics() - now "
            "able to initialize PulseAudio",
            this, aGlobalObject);
        ReportAnalysis(aGlobalObject, sCannotInitializePulseAudio, true,
                       u"*"_ns);
      }
      break;
  }
}

void DecoderDoctorDiagnostics::StoreDecodeError(nsIGlobalObject* aGlobalObject,
                                                const MediaResult& aError,
                                                const nsString& aMediaSrc,
                                                const char* aCallSite) {
  // Make sure we only store once.
  MOZ_ASSERT(mDiagnosticsType == eUnsaved);
  mDiagnosticsType = eDecodeError;

  if (NS_WARN_IF(aError.Message().Length() > 2048)) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreDecodeError(nsIGlobalObject* "
        "aGlobalObject=%p, aError= TOO LONG! '%s', aMediaSrc=<provided>, call "
        "site "
        "'%s')",
        aGlobalObject, this, aError.Description().get(), aCallSite);
    return;
  }

  if (NS_WARN_IF(aMediaSrc.Length() > 2048)) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreDecodeError(nsIGlobalObject* "
        "aGlobalObject=%p, aError=%s, aMediaSrc= TOO LONG! <provided>, call "
        "site "
        "'%s')",
        aGlobalObject, this, aError.Description().get(), aCallSite);
    return;
  }

  if (NS_WARN_IF(!aGlobalObject)) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreDecodeError("
        "nsIGlobalObject* aGlobalObject=nullptr, aError=%s,"
        " aMediaSrc=<provided>, call site '%s')",
        this, aError.Description().get(), aCallSite);
    return;
  }

  RefPtr<DecoderDoctorGlobalWatcher> watcher =
      DecoderDoctorGlobalWatcher::RetrieveOrCreate(aGlobalObject);

  if (NS_WARN_IF(!watcher)) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreDecodeError("
        "nsIGlobalObject* aGlobalObject=%p, aError='%s', aMediaSrc=<provided>,"
        " call site '%s') - Could not create document watcher",
        this, aGlobalObject, aError.Description().get(), aCallSite);
    return;
  }

  mDecodeIssue = aError;
  mDecodeIssueMediaSrc = aMediaSrc;

  // StoreDecodeError should only be called once, after all data is
  // available, so it is safe to std::move() from this object.
  watcher->AddDiagnostics(std::move(*this), aCallSite);
  // Even though it's moved-from, the type should stay set
  // (Only used to ensure that we do store only once.)
  MOZ_ASSERT(mDiagnosticsType == eDecodeError);
}

void DecoderDoctorDiagnostics::StoreDecodeWarning(
    nsIGlobalObject* aGlobalObject, const MediaResult& aWarning,
    const nsString& aMediaSrc, const char* aCallSite) {
  MOZ_ASSERT(NS_IsMainThread());
  // Make sure we only store once.
  MOZ_ASSERT(mDiagnosticsType == eUnsaved);
  mDiagnosticsType = eDecodeWarning;

  if (NS_WARN_IF(!aGlobalObject)) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreDecodeWarning("
        "nsIGlobalObject* aGlobalObject=nullptr, aWarning=%s,"
        " aMediaSrc=<provided>, call site '%s')",
        this, aWarning.Description().get(), aCallSite);
    return;
  }

  RefPtr<DecoderDoctorGlobalWatcher> watcher =
      DecoderDoctorGlobalWatcher::RetrieveOrCreate(aGlobalObject);

  if (NS_WARN_IF(!watcher)) {
    DD_WARN(
        "DecoderDoctorDiagnostics[%p]::StoreDecodeWarning("
        "nsIGlobalObject* aGlobalObject=%p, aWarning='%s', "
        "aMediaSrc=<provided>,"
        " call site '%s') - Could not create document watcher",
        this, aGlobalObject, aWarning.Description().get(), aCallSite);
    return;
  }

  mDecodeIssue = aWarning;
  mDecodeIssueMediaSrc = aMediaSrc;

  // StoreDecodeWarning should only be called once, after all data is
  // available, so it is safe to std::move() from this object.
  watcher->AddDiagnostics(std::move(*this), aCallSite);
  // Even though it's moved-from, the type should stay set
  // (Only used to ensure that we do store only once.)
  MOZ_ASSERT(mDiagnosticsType == eDecodeWarning);
}

static const char* EventDomainString(DecoderDoctorEvent::Domain aDomain) {
  switch (aDomain) {
    case DecoderDoctorEvent::eAudioSinkStartup:
      return "audio-sink-startup";
  }
  return "?";
}

nsCString DecoderDoctorDiagnostics::GetDescription() const {
  nsCString s;
  switch (mDiagnosticsType) {
    case eUnsaved:
      s = "Unsaved diagnostics, cannot get accurate description";
      break;
    case eFormatSupportCheck:
      s = "format='";
      s += NS_ConvertUTF16toUTF8(mFormat).get();
      s += mFlags.contains(Flags::CanPlay) ? "', can play" : "', cannot play";
      if (mFlags.contains(Flags::VideoNotSupported)) {
        s += ", but video format not supported";
      }
      if (mFlags.contains(Flags::AudioNotSupported)) {
        s += ", but audio format not supported";
      }
      if (mFlags.contains(Flags::WMFFailedToLoad)) {
        s += ", Windows platform decoder failed to load";
      }
      if (mFlags.contains(Flags::FFmpegNotFound)) {
        s += ", Linux platform decoder not found";
      }
      if (mFlags.contains(Flags::GMPPDMFailedToStartup)) {
        s += ", GMP PDM failed to startup";
      } else if (!mGMP.IsEmpty()) {
        s += ", Using GMP '";
        s += mGMP;
        s += "'";
      }
      break;
    case eMediaKeySystemAccessRequest:
      s = "key system='";
      s += NS_ConvertUTF16toUTF8(mKeySystem).get();
      s += mIsKeySystemSupported ? "', supported" : "', not supported";
      switch (mKeySystemIssue) {
        case eUnset:
          break;
        case eWidevineWithNoWMF:
          s += ", Widevine with no WMF";
          break;
      }
      break;
    case eEvent:
      s = nsPrintfCString("event domain %s result=%" PRIu32,
                          EventDomainString(mEvent.mDomain),
                          static_cast<uint32_t>(mEvent.mResult));
      break;
    case eDecodeError:
      s = "decode error: ";
      s += mDecodeIssue.Description();
      s += ", src='";
      s += mDecodeIssueMediaSrc.IsEmpty() ? "<none>" : "<provided>";
      s += "'";
      break;
    case eDecodeWarning:
      s = "decode warning: ";
      s += mDecodeIssue.Description();
      s += ", src='";
      s += mDecodeIssueMediaSrc.IsEmpty() ? "<none>" : "<provided>";
      s += "'";
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected DiagnosticsType");
      s = "?";
      break;
  }
  return s;
}

static const char* ToDecoderDoctorReportTypeStr(
    const dom::DecoderDoctorReportType& aType) {
  switch (aType) {
    case dom::DecoderDoctorReportType::Mediawidevinenowmf:
      return sMediaWidevineNoWMF.mReportStringId;
    case dom::DecoderDoctorReportType::Mediawmfneeded:
      return sMediaWMFNeeded.mReportStringId;
    case dom::DecoderDoctorReportType::Mediaplatformdecodernotfound:
      return sMediaFFMpegNotFound.mReportStringId;
    case dom::DecoderDoctorReportType::Mediacannotplaynodecoders:
      return sMediaCannotPlayNoDecoders.mReportStringId;
    case dom::DecoderDoctorReportType::Medianodecoders:
      return sMediaNoDecoders.mReportStringId;
    case dom::DecoderDoctorReportType::Mediacannotinitializepulseaudio:
      return sCannotInitializePulseAudio.mReportStringId;
    case dom::DecoderDoctorReportType::Mediaunsupportedlibavcodec:
      return sUnsupportedLibavcodec.mReportStringId;
    case dom::DecoderDoctorReportType::Mediadecodeerror:
      return sMediaDecodeError.mReportStringId;
    case dom::DecoderDoctorReportType::Mediadecodewarning:
      return sMediaDecodeWarning.mReportStringId;
    default:
      DD_DEBUG("Invalid report type to str");
      return "invalid-report-type";
  }
}

void DecoderDoctorDiagnostics::SetDecoderDoctorReportType(
    const dom::DecoderDoctorReportType& aType) {
  DD_INFO("Set report type %s", ToDecoderDoctorReportTypeStr(aType));
  switch (aType) {
    case dom::DecoderDoctorReportType::Mediawmfneeded:
      SetWMFFailedToLoad();
      return;
    case dom::DecoderDoctorReportType::Mediaplatformdecodernotfound:
      SetFFmpegNotFound();
      return;
    case dom::DecoderDoctorReportType::Mediaunsupportedlibavcodec:
      SetLibAVCodecUnsupported();
      return;
    case dom::DecoderDoctorReportType::Mediacannotplaynodecoders:
    case dom::DecoderDoctorReportType::Medianodecoders:
      // Do nothing, because these type are related with can-play, which would
      // be handled in `StoreFormatDiagnostics()` when sending `false` in the
      // parameter for the canplay.
      return;
    default:
      DD_DEBUG("Not supported type");
      return;
  }
}

}  // namespace mozilla
