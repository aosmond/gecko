/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ArrayUtils.h"

#include "nsAnnotationService.h"
#include "nsNavHistory.h"
#include "nsPlacesTables.h"
#include "nsPlacesIndexes.h"
#include "nsPlacesMacros.h"
#include "Helpers.h"

#include "nsNetUtil.h"
#include "nsIVariant.h"
#include "nsString.h"
#include "nsVariant.h"
#include "mozilla/storage.h"

#include "GeckoProfiler.h"

#include "nsNetCID.h"

using namespace mozilla;
using namespace mozilla::places;

#define ENSURE_ANNO_TYPE(_type, _statement)                                    \
  PR_BEGIN_MACRO                                                               \
  int32_t type = _statement->AsInt32(kAnnoIndex_Type);                         \
  NS_ENSURE_TRUE(type == nsIAnnotationService::_type, NS_ERROR_INVALID_ARG);   \
  PR_END_MACRO

const int32_t nsAnnotationService::kAnnoIndex_ID = 0;
const int32_t nsAnnotationService::kAnnoIndex_PageOrItem = 1;
const int32_t nsAnnotationService::kAnnoIndex_NameID = 2;
const int32_t nsAnnotationService::kAnnoIndex_Content = 3;
const int32_t nsAnnotationService::kAnnoIndex_Flags = 4;
const int32_t nsAnnotationService::kAnnoIndex_Expiration = 5;
const int32_t nsAnnotationService::kAnnoIndex_Type = 6;
const int32_t nsAnnotationService::kAnnoIndex_DateAdded = 7;
const int32_t nsAnnotationService::kAnnoIndex_LastModified = 8;

namespace {

// Fires `onItemChanged` notifications for bookmark annotation changes.
void
NotifyItemChanged(const BookmarkData& aBookmark, const nsACString& aName,
                  uint16_t aSource, bool aDontUpdateLastModified)
{
  if (aBookmark.id < 0) {
    return;
  }

  ItemChangeData changeData;
  changeData.bookmark = aBookmark;
  changeData.isAnnotation = true;
  changeData.updateLastModified = !aDontUpdateLastModified;
  changeData.source = aSource;
  changeData.property = aName;

  nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
  if (bookmarks) {
    bookmarks->NotifyItemChanged(changeData);
  }
}

}

namespace mozilla {
namespace places {

////////////////////////////////////////////////////////////////////////////////
//// AnnotatedResult

AnnotatedResult::AnnotatedResult(const nsCString& aGUID,
                                 nsIURI* aURI,
                                 int64_t aItemId,
                                 const nsACString& aAnnotationName,
                                 nsIVariant* aAnnotationValue)
: mGUID(aGUID)
, mURI(aURI)
, mItemId(aItemId)
, mAnnotationName(aAnnotationName)
, mAnnotationValue(aAnnotationValue)
{
}

AnnotatedResult::~AnnotatedResult()
{
}

NS_IMETHODIMP
AnnotatedResult::GetGuid(nsACString& _guid)
{
  _guid = mGUID;
  return NS_OK;
}

NS_IMETHODIMP
AnnotatedResult::GetUri(nsIURI** _uri)
{
  NS_IF_ADDREF(*_uri = mURI);
  return NS_OK;
}

NS_IMETHODIMP
AnnotatedResult::GetItemId(int64_t* _itemId)
{
  *_itemId = mItemId;
  return NS_OK;
}

NS_IMETHODIMP
AnnotatedResult::GetAnnotationName(nsACString& _annotationName)
{
  _annotationName = mAnnotationName;
  return NS_OK;
}

NS_IMETHODIMP
AnnotatedResult::GetAnnotationValue(nsIVariant** _annotationValue)
{
  NS_IF_ADDREF(*_annotationValue = mAnnotationValue);
  return NS_OK;
}

NS_IMPL_ISUPPORTS(AnnotatedResult, mozIAnnotatedResult)

} // namespace places
} // namespace mozilla

PLACES_FACTORY_SINGLETON_IMPLEMENTATION(nsAnnotationService, gAnnotationService)

NS_IMPL_ISUPPORTS(nsAnnotationService
, nsIAnnotationService
, nsISupportsWeakReference
)


nsAnnotationService::nsAnnotationService()
{
  NS_ASSERTION(!gAnnotationService,
               "Attempting to create two instances of the service!");
  gAnnotationService = this;
}


nsAnnotationService::~nsAnnotationService()
{
  NS_ASSERTION(gAnnotationService == this,
               "Deleting a non-singleton instance of the service");
  if (gAnnotationService == this)
    gAnnotationService = nullptr;
}


nsresult
nsAnnotationService::Init()
{
  mDB = Database::GetDatabase();
  NS_ENSURE_STATE(mDB);

  return NS_OK;
}

nsresult
nsAnnotationService::SetAnnotationStringInternal(int64_t aItemId,
                                                 BookmarkData* aBookmark,
                                                 const nsACString& aName,
                                                 const nsAString& aValue,
                                                 int32_t aFlags,
                                                 uint16_t aExpiration)
{
  mozStorageTransaction transaction(mDB->MainConn(), false);
  nsCOMPtr<mozIStorageStatement> statement;

  nsresult rv = StartSetAnnotation(aItemId, aBookmark, aName, aFlags,
                                   aExpiration,
                                   nsIAnnotationService::TYPE_STRING,
                                   statement);
  NS_ENSURE_SUCCESS(rv, rv);

  mozStorageStatementScoper scoper(statement);

  rv = statement->BindStringByName(NS_LITERAL_CSTRING("content"), aValue);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::SetItemAnnotation(int64_t aItemId,
                                       const nsACString& aName,
                                       nsIVariant* aValue,
                                       int32_t aFlags,
                                       uint16_t aExpiration,
                                       uint16_t aSource,
                                       bool aDontUpdateLastModified)
{
  AUTO_PROFILER_LABEL("nsAnnotationService::SetItemAnnotation", OTHER);

  NS_ENSURE_ARG_MIN(aItemId, 1);
  NS_ENSURE_ARG(aValue);

  uint16_t dataType;
  nsresult rv = aValue->GetDataType(&dataType);
  NS_ENSURE_SUCCESS(rv, rv);
  BookmarkData bookmark;

  switch (dataType) {
    case nsIDataType::VTYPE_INT8:
    case nsIDataType::VTYPE_UINT8:
    case nsIDataType::VTYPE_INT16:
    case nsIDataType::VTYPE_UINT16:
    case nsIDataType::VTYPE_INT32:
    case nsIDataType::VTYPE_UINT32:
    case nsIDataType::VTYPE_BOOL: {
      int32_t valueInt;
      rv = aValue->GetAsInt32(&valueInt);
      if (NS_SUCCEEDED(rv)) {
        NS_ENSURE_SUCCESS(rv, rv);
        rv = SetAnnotationInt32Internal(aItemId, &bookmark, aName,
                                        valueInt, aFlags, aExpiration);
        NS_ENSURE_SUCCESS(rv, rv);
        break;
      }
      // Fall through int64_t case otherwise.
      MOZ_FALLTHROUGH;
    }
    case nsIDataType::VTYPE_INT64:
    case nsIDataType::VTYPE_UINT64: {
      int64_t valueLong;
      rv = aValue->GetAsInt64(&valueLong);
      if (NS_SUCCEEDED(rv)) {
        NS_ENSURE_SUCCESS(rv, rv);
        rv = SetAnnotationInt64Internal(aItemId, &bookmark, aName,
                                        valueLong, aFlags, aExpiration);
        NS_ENSURE_SUCCESS(rv, rv);
        break;
      }
      // Fall through double case otherwise.
      MOZ_FALLTHROUGH;
    }
    case nsIDataType::VTYPE_FLOAT:
    case nsIDataType::VTYPE_DOUBLE: {
      double valueDouble;
      rv = aValue->GetAsDouble(&valueDouble);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = SetAnnotationDoubleInternal(aItemId, &bookmark,
                                       aName, valueDouble, aFlags, aExpiration);
      NS_ENSURE_SUCCESS(rv, rv);
      break;
    }
    case nsIDataType::VTYPE_CHAR:
    case nsIDataType::VTYPE_WCHAR:
    case nsIDataType::VTYPE_DOMSTRING:
    case nsIDataType::VTYPE_CHAR_STR:
    case nsIDataType::VTYPE_WCHAR_STR:
    case nsIDataType::VTYPE_STRING_SIZE_IS:
    case nsIDataType::VTYPE_WSTRING_SIZE_IS:
    case nsIDataType::VTYPE_UTF8STRING:
    case nsIDataType::VTYPE_CSTRING:
    case nsIDataType::VTYPE_ASTRING: {
      nsAutoString stringValue;
      rv = aValue->GetAsAString(stringValue);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = SetAnnotationStringInternal(aItemId, &bookmark, aName,
                                       stringValue, aFlags, aExpiration);
      NS_ENSURE_SUCCESS(rv, rv);
      break;
    }
    default:
      return NS_ERROR_NOT_IMPLEMENTED;
  }

  NotifyItemChanged(bookmark, aName, aSource, aDontUpdateLastModified);

  return NS_OK;
}


nsresult
nsAnnotationService::SetAnnotationInt32Internal(int64_t aItemId,
                                                BookmarkData* aBookmark,
                                                const nsACString& aName,
                                                int32_t aValue,
                                                int32_t aFlags,
                                                uint16_t aExpiration)
{
  mozStorageTransaction transaction(mDB->MainConn(), false);
  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartSetAnnotation(aItemId, aBookmark, aName, aFlags,
                                   aExpiration,
                                   nsIAnnotationService::TYPE_INT32,
                                   statement);
  NS_ENSURE_SUCCESS(rv, rv);

  mozStorageStatementScoper scoper(statement);

  rv = statement->BindInt32ByName(NS_LITERAL_CSTRING("content"), aValue);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


nsresult
nsAnnotationService::SetAnnotationInt64Internal(int64_t aItemId,
                                                BookmarkData* aBookmark,
                                                const nsACString& aName,
                                                int64_t aValue,
                                                int32_t aFlags,
                                                uint16_t aExpiration)
{
  mozStorageTransaction transaction(mDB->MainConn(), false);
  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartSetAnnotation(aItemId, aBookmark, aName, aFlags,
                                   aExpiration,
                                   nsIAnnotationService::TYPE_INT64,
                                   statement);
  NS_ENSURE_SUCCESS(rv, rv);

  mozStorageStatementScoper scoper(statement);

  rv = statement->BindInt64ByName(NS_LITERAL_CSTRING("content"), aValue);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


nsresult
nsAnnotationService::SetAnnotationDoubleInternal(int64_t aItemId,
                                                 BookmarkData* aBookmark,
                                                 const nsACString& aName,
                                                 double aValue,
                                                 int32_t aFlags,
                                                 uint16_t aExpiration)
{
  mozStorageTransaction transaction(mDB->MainConn(), false);
  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartSetAnnotation(aItemId, aBookmark, aName, aFlags,
                                   aExpiration,
                                   nsIAnnotationService::TYPE_DOUBLE,
                                   statement);
  NS_ENSURE_SUCCESS(rv, rv);

  mozStorageStatementScoper scoper(statement);

  rv = statement->BindDoubleByName(NS_LITERAL_CSTRING("content"), aValue);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetPageAnnotation(nsIURI* aURI,
                                       const nsACString& aName,
                                       nsIVariant** _retval)
{
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG_POINTER(_retval);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(aURI, 0, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);

  nsCOMPtr<nsIWritableVariant> value = new nsVariant();
  int32_t type = statement->AsInt32(kAnnoIndex_Type);
  switch (type) {
    case nsIAnnotationService::TYPE_INT32:
    case nsIAnnotationService::TYPE_INT64:
    case nsIAnnotationService::TYPE_DOUBLE: {
      rv = value->SetAsDouble(statement->AsDouble(kAnnoIndex_Content));
      break;
    }
    case nsIAnnotationService::TYPE_STRING: {
      nsAutoString valueString;
      rv = statement->GetString(kAnnoIndex_Content, valueString);
      if (NS_SUCCEEDED(rv))
        rv = value->SetAsAString(valueString);
      break;
    }
    default: {
      rv = NS_ERROR_UNEXPECTED;
      break;
    }
  }

  if (NS_SUCCEEDED(rv)) {
    value.forget(_retval);
  }

  return rv;
}

nsresult
nsAnnotationService::GetValueFromStatement(nsCOMPtr<mozIStorageStatement>& aStatement,
                                           nsIVariant** _retval)
{
  nsresult rv;

  nsCOMPtr<nsIWritableVariant> value = new nsVariant();
  int32_t type = aStatement->AsInt32(kAnnoIndex_Type);
  switch (type) {
    case nsIAnnotationService::TYPE_INT32:
    case nsIAnnotationService::TYPE_INT64:
    case nsIAnnotationService::TYPE_DOUBLE: {
      rv = value->SetAsDouble(aStatement->AsDouble(kAnnoIndex_Content));
      break;
    }
    case nsIAnnotationService::TYPE_STRING: {
      nsAutoString valueString;
      rv = aStatement->GetString(kAnnoIndex_Content, valueString);
      if (NS_SUCCEEDED(rv))
        rv = value->SetAsAString(valueString);
      break;
    }
    default: {
      rv = NS_ERROR_UNEXPECTED;
      break;
    }
  }
  if (NS_SUCCEEDED(rv)) {
    value.forget(_retval);
  }
  return rv;
}


NS_IMETHODIMP
nsAnnotationService::GetItemAnnotation(int64_t aItemId,
                                       const nsACString& aName,
                                       nsIVariant** _retval)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);
  NS_ENSURE_ARG_POINTER(_retval);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(nullptr, aItemId, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);

  return GetValueFromStatement(statement, _retval);
}


NS_IMETHODIMP
nsAnnotationService::GetItemAnnotationInfo(int64_t aItemId,
                                           const nsACString& aName,
                                           nsIVariant** _value,
                                           int32_t* _flags,
                                           uint16_t* _expiration,
                                           uint16_t* _storageType)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);
  NS_ENSURE_ARG_POINTER(_value);
  NS_ENSURE_ARG_POINTER(_flags);
  NS_ENSURE_ARG_POINTER(_expiration);
  NS_ENSURE_ARG_POINTER(_storageType);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(nullptr, aItemId, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  *_flags = statement->AsInt32(kAnnoIndex_Flags);
  *_expiration = (uint16_t)statement->AsInt32(kAnnoIndex_Expiration);
  int32_t type = (uint16_t)statement->AsInt32(kAnnoIndex_Type);
  if (type == 0) {
    // For annotations created before explicit typing,
    // we can't determine type, just return as string type.
    *_storageType = nsIAnnotationService::TYPE_STRING;
  }
  else {
    *_storageType = type;
  }

  return GetValueFromStatement(statement, _value);
}


NS_IMETHODIMP
nsAnnotationService::GetAnnotationsWithName(const nsACString& aName,
                                            uint32_t* _count,
                                            mozIAnnotatedResult*** _annotations)
{
  NS_ENSURE_ARG(!aName.IsEmpty());
  NS_ENSURE_ARG_POINTER(_annotations);

  *_count = 0;
  *_annotations = nullptr;
  nsCOMArray<mozIAnnotatedResult> annotations;

  nsCOMPtr<mozIStorageStatement> stmt = mDB->GetStatement(
    "SELECT h.guid, h.url, -1, a.type, a.content "
    "FROM moz_anno_attributes n "
    "JOIN moz_annos a ON n.id = a.anno_attribute_id "
    "JOIN moz_places h ON h.id = a.place_id "
    "WHERE n.name = :anno_name "
    "UNION ALL "
    "SELECT b.guid, h.url, b.id, a.type, a.content "
    "FROM moz_anno_attributes n "
    "JOIN moz_items_annos a ON n.id = a.anno_attribute_id "
    "JOIN moz_bookmarks b ON b.id = a.item_id "
    "LEFT JOIN moz_places h ON h.id = b.fk "
    "WHERE n.name = :anno_name "
  );
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindUTF8StringByName(NS_LITERAL_CSTRING("anno_name"),
                                           aName);
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasMore = false;
  while (NS_SUCCEEDED(rv = stmt->ExecuteStep(&hasMore)) && hasMore) {
    nsAutoCString guid;
    rv = stmt->GetUTF8String(0, guid);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIURI> uri;
    bool uriIsNull = false;
    rv = stmt->GetIsNull(1, &uriIsNull);
    NS_ENSURE_SUCCESS(rv, rv);
    if (!uriIsNull) {
      nsAutoCString url;
      rv = stmt->GetUTF8String(1, url);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = NS_NewURI(getter_AddRefs(uri), url);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    int64_t itemId = stmt->AsInt64(2);
    int32_t type = stmt->AsInt32(3);

    nsCOMPtr<nsIWritableVariant> variant = new nsVariant();
    switch (type) {
      case nsIAnnotationService::TYPE_INT32: {
        rv = variant->SetAsInt32(stmt->AsInt32(4));
        break;
      }
      case nsIAnnotationService::TYPE_INT64: {
        rv = variant->SetAsInt64(stmt->AsInt64(4));
        break;
      }
      case nsIAnnotationService::TYPE_DOUBLE: {
        rv = variant->SetAsDouble(stmt->AsDouble(4));
        break;
      }
      case nsIAnnotationService::TYPE_STRING: {
        nsAutoString valueString;
        rv = stmt->GetString(4, valueString);
        NS_ENSURE_SUCCESS(rv, rv);

        rv = variant->SetAsAString(valueString);
        break;
      }
      default:
        MOZ_ASSERT(false, "Unsupported annotation type");
        // Move to the next result.
        continue;
    }
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<mozIAnnotatedResult> anno = new AnnotatedResult(guid, uri, itemId,
                                                             aName, variant);
    NS_ENSURE_TRUE(annotations.AppendObject(anno), NS_ERROR_OUT_OF_MEMORY);
  }

  // Convert to raw array.
  if (annotations.Count() == 0)
    return NS_OK;

  *_count = annotations.Count();
  annotations.Forget(_annotations);

  return NS_OK;
}


nsresult
nsAnnotationService::GetItemAnnotationNamesTArray(int64_t aItemId,
                                                  nsTArray<nsCString>* _result)
{
  _result->Clear();

  nsCOMPtr<mozIStorageStatement> statement;
  statement = mDB->GetStatement(
    "SELECT n.name "
    "FROM moz_anno_attributes n "
    "JOIN moz_items_annos a ON a.anno_attribute_id = n.id "
    "WHERE a.item_id = :item_id"
  );
  NS_ENSURE_STATE(statement);
  mozStorageStatementScoper scoper(statement);

  nsresult rv = statement->BindInt64ByName(NS_LITERAL_CSTRING("item_id"), aItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult = false;
  while (NS_SUCCEEDED(statement->ExecuteStep(&hasResult)) &&
         hasResult) {
    nsAutoCString name;
    rv = statement->GetUTF8String(0, name);
    NS_ENSURE_SUCCESS(rv, rv);
    if (!_result->AppendElement(name))
      return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetItemAnnotationNames(int64_t aItemId,
                                            uint32_t* _count,
                                            nsIVariant*** _result)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);
  NS_ENSURE_ARG_POINTER(_count);
  NS_ENSURE_ARG_POINTER(_result);

  *_count = 0;
  *_result = nullptr;

  nsTArray<nsCString> names;
  nsresult rv = GetItemAnnotationNamesTArray(aItemId, &names);
  NS_ENSURE_SUCCESS(rv, rv);

  if (names.Length() == 0)
    return NS_OK;

  *_result = static_cast<nsIVariant**>
                        (moz_xmalloc(sizeof(nsIVariant*) * names.Length()));
  NS_ENSURE_TRUE(*_result, NS_ERROR_OUT_OF_MEMORY);

  for (uint32_t i = 0; i < names.Length(); i ++) {
    nsCOMPtr<nsIWritableVariant> var = new nsVariant();
    if (!var) {
      // need to release all the variants we've already created
      for (uint32_t j = 0; j < i; j ++)
        NS_RELEASE((*_result)[j]);
      free(*_result);
      *_result = nullptr;
      return NS_ERROR_OUT_OF_MEMORY;
    }
    var->SetAsAUTF8String(names[i]);
    NS_ADDREF((*_result)[i] = var);
  }
  *_count = names.Length();

  return NS_OK;
}


/**
 * @note We don't remove anything from the moz_anno_attributes table. If we
 *       delete the last item of a given name, that item really should go away.
 *       It will be cleaned up by expiration.
 */
nsresult
nsAnnotationService::RemoveAnnotationInternal(int64_t aItemId,
                                              BookmarkData* aBookmark,
                                              const nsACString& aName)
{
  nsCOMPtr<mozIStorageStatement> statement;
  statement = mDB->GetStatement(
    "DELETE FROM moz_items_annos "
    "WHERE item_id = :item_id "
      "AND anno_attribute_id = "
        "(SELECT id FROM moz_anno_attributes WHERE name = :anno_name)"
  );
  NS_ENSURE_STATE(statement);
  mozStorageStatementScoper scoper(statement);

  nsresult rv;
  rv = statement->BindInt64ByName(NS_LITERAL_CSTRING("item_id"), aItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->BindUTF8StringByName(NS_LITERAL_CSTRING("anno_name"), aName);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
  if (bookmarks) {
    MOZ_ASSERT(aBookmark);
    if (NS_FAILED(bookmarks->FetchItemInfo(aItemId, *aBookmark))) {
      aBookmark->id = -1;
    }
  }

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::RemoveItemAnnotation(int64_t aItemId,
                                          const nsACString& aName,
                                          uint16_t aSource)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);

  BookmarkData bookmark;
  nsresult rv = RemoveAnnotationInternal(aItemId, &bookmark, aName);
  NS_ENSURE_SUCCESS(rv, rv);

  NotifyItemChanged(bookmark, aName, aSource, false);

  return NS_OK;
}


nsresult
nsAnnotationService::RemoveItemAnnotations(int64_t aItemId)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);

  // Should this be precompiled or a getter?
  nsCOMPtr<mozIStorageStatement> statement = mDB->GetStatement(
    "DELETE FROM moz_items_annos WHERE item_id = :item_id"
  );
  NS_ENSURE_STATE(statement);
  mozStorageStatementScoper scoper(statement);

  nsresult rv = statement->BindInt64ByName(NS_LITERAL_CSTRING("item_id"), aItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::ItemHasAnnotation(int64_t aItemId,
                                       const nsACString& aName,
                                       bool* _hasAnno)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);
  NS_ENSURE_ARG_POINTER(_hasAnno);

  nsCOMPtr<mozIStorageStatement> stmt = mDB->GetStatement(
    "SELECT b.id, "
           "(SELECT id FROM moz_anno_attributes WHERE name = :anno_name) AS nameid, "
           "a.id, a.dateAdded "
    "FROM moz_bookmarks b "
    "LEFT JOIN moz_items_annos a ON a.item_id = b.id "
                               "AND a.anno_attribute_id = nameid "
    "WHERE b.id = :item_id"
  );
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper checkAnnoScoper(stmt);

  nsresult rv = stmt->BindUTF8StringByName(NS_LITERAL_CSTRING("anno_name"), aName);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("item_id"), aItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!hasResult) {
    // We are trying to get an annotation on an invalid bookmarks or
    // history entry.
    // Here we preserve the old behavior, returning that we don't have the
    // annotation, ignoring the fact itemId is invalid.
    // Otherwise we should return NS_ERROR_INVALID_ARG, but this will somehow
    // break the API.  In future we could want to be pickier.
    *_hasAnno = false;
  }
  else {
    int64_t annotationId = stmt->AsInt64(2);
    *_hasAnno = (annotationId > 0);
  }

  return NS_OK;
}


/**
 * This loads the statement and steps it once so you can get data out of it.
 *
 * @note You have to reset the statement when you're done if this succeeds.
 * @throws NS_ERROR_NOT_AVAILABLE if the annotation is not found.
 */

nsresult
nsAnnotationService::StartGetAnnotation(nsIURI* aURI,
                                        int64_t aItemId,
                                        const nsACString& aName,
                                        nsCOMPtr<mozIStorageStatement>& aStatement)
{
  bool isItemAnnotation = (aItemId > 0);

  if (isItemAnnotation) {
    aStatement = mDB->GetStatement(
      "SELECT a.id, a.item_id, :anno_name, a.content, a.flags, "
             "a.expiration, a.type "
      "FROM moz_anno_attributes n "
      "JOIN moz_items_annos a ON a.anno_attribute_id = n.id "
      "WHERE a.item_id = :item_id "
      "AND n.name = :anno_name"
    );
  }
  else {
    aStatement = mDB->GetStatement(
      "SELECT a.id, a.place_id, :anno_name, a.content, a.flags, "
             "a.expiration, a.type "
      "FROM moz_anno_attributes n "
      "JOIN moz_annos a ON n.id = a.anno_attribute_id "
      "JOIN moz_places h ON h.id = a.place_id "
      "WHERE h.url_hash = hash(:page_url) AND h.url = :page_url "
        "AND n.name = :anno_name"
    );
  }
  NS_ENSURE_STATE(aStatement);
  mozStorageStatementScoper getAnnoScoper(aStatement);

  nsresult rv;
  if (isItemAnnotation)
    rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("item_id"), aItemId);
  else
    rv = URIBinder::Bind(aStatement, NS_LITERAL_CSTRING("page_url"), aURI);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aStatement->BindUTF8StringByName(NS_LITERAL_CSTRING("anno_name"), aName);
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult = false;
  rv = aStatement->ExecuteStep(&hasResult);
  if (NS_FAILED(rv) || !hasResult)
    return NS_ERROR_NOT_AVAILABLE;

  // on success, DON'T reset the statement, the caller needs to read from it,
  // and it is the caller's job to reset it.
  getAnnoScoper.Abandon();

  return NS_OK;
}


/**
 * This does most of the setup work needed to set an annotation, except for
 * binding the the actual value and executing the statement.
 * It will either update an existing annotation or insert a new one.
 *
 * @note The aStatement RESULT IS NOT ADDREFED.  This is just one of the class
 *       vars, which control its scope.  DO NOT RELEASE.
 *       The caller must take care of resetting the statement if this succeeds.
 */
nsresult
nsAnnotationService::StartSetAnnotation(int64_t aItemId,
                                        BookmarkData* aBookmark,
                                        const nsACString& aName,
                                        int32_t aFlags,
                                        uint16_t aExpiration,
                                        uint16_t aType,
                                        nsCOMPtr<mozIStorageStatement>& aStatement)
{
  MOZ_ASSERT(aExpiration == EXPIRE_NEVER, "Only EXPIRE_NEVER is supported");
  NS_ENSURE_ARG(aExpiration == EXPIRE_NEVER);

  // Ensure the annotation name exists.
  nsCOMPtr<mozIStorageStatement> addNameStmt = mDB->GetStatement(
    "INSERT OR IGNORE INTO moz_anno_attributes (name) VALUES (:anno_name)"
  );
  NS_ENSURE_STATE(addNameStmt);
  mozStorageStatementScoper scoper(addNameStmt);

  nsresult rv = addNameStmt->BindUTF8StringByName(NS_LITERAL_CSTRING("anno_name"), aName);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = addNameStmt->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  // We have to check 2 things:
  // - if the annotation already exists we should update it.
  // - we should not allow setting annotations on invalid URIs or itemIds.
  // This query will tell us:
  // - whether the item or page exists.
  // - whether the annotation already exists.
  // - the nameID associated with the annotation name.
  // - the id and dateAdded of the old annotation, if it exists.
  nsCOMPtr<mozIStorageStatement> stmt = mDB->GetStatement(
    "SELECT b.id, "
           "(SELECT id FROM moz_anno_attributes WHERE name = :anno_name) AS nameid, "
           "a.id, a.dateAdded, b.parent, b.type, b.lastModified, b.guid, p.guid "
    "FROM moz_bookmarks b "
    "JOIN moz_bookmarks p ON p.id = b.parent "
    "LEFT JOIN moz_items_annos a ON a.item_id = b.id "
                               "AND a.anno_attribute_id = nameid "
    "WHERE b.id = :item_id"
  );
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper checkAnnoScoper(stmt);

  rv = stmt->BindUTF8StringByName(NS_LITERAL_CSTRING("anno_name"), aName);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("item_id"), aItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!hasResult) {
    // We are trying to create an annotation on an invalid bookmark
    // or history entry.
    return NS_ERROR_INVALID_ARG;
  }

  int64_t fkId = stmt->AsInt64(0);
  int64_t nameID = stmt->AsInt64(1);
  int64_t oldAnnoId = stmt->AsInt64(2);
  int64_t oldAnnoDate = stmt->AsInt64(3);

  aStatement = mDB->GetStatement(
    "INSERT OR REPLACE INTO moz_items_annos "
      "(id, item_id, anno_attribute_id, content, flags, "
       "expiration, type, dateAdded, lastModified) "
    "VALUES (:id, :fk, :name_id, :content, :flags, "
    ":expiration, :type, :date_added, :last_modified)"
  );

  // Since we're already querying `moz_bookmarks`, we fetch the changed
  // bookmark's info here, instead of using `FetchItemInfo`.
  MOZ_ASSERT(aBookmark);
  aBookmark->id = fkId;
  aBookmark->parentId = stmt->AsInt64(4);
  aBookmark->type = stmt->AsInt64(5);

  aBookmark->lastModified = static_cast<PRTime>(stmt->AsInt64(6));
  if (NS_FAILED(stmt->GetUTF8String(7, aBookmark->guid)) ||
      NS_FAILED(stmt->GetUTF8String(8, aBookmark->parentGuid))) {
    aBookmark->id = -1;
  }
  NS_ENSURE_STATE(aStatement);
  mozStorageStatementScoper setAnnoScoper(aStatement);

  // Don't replace existing annotations.
  if (oldAnnoId > 0) {
    rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("id"), oldAnnoId);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("date_added"), oldAnnoDate);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    rv = aStatement->BindNullByName(NS_LITERAL_CSTRING("id"));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("date_added"), RoundedPRNow());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("fk"), fkId);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("name_id"), nameID);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aStatement->BindInt32ByName(NS_LITERAL_CSTRING("flags"), aFlags);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aStatement->BindInt32ByName(NS_LITERAL_CSTRING("expiration"), aExpiration);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aStatement->BindInt32ByName(NS_LITERAL_CSTRING("type"), aType);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("last_modified"), RoundedPRNow());
  NS_ENSURE_SUCCESS(rv, rv);

  // On success, leave the statement open, the caller will set the value
  // and execute the statement.
  setAnnoScoper.Abandon();

  return NS_OK;
}
