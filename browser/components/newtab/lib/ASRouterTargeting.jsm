ChromeUtils.import("resource://gre/modules/components-utils/FilterExpressions.jsm");
ChromeUtils.import("resource://gre/modules/Services.jsm");
ChromeUtils.defineModuleGetter(this, "AddonManager",
  "resource://gre/modules/AddonManager.jsm");
ChromeUtils.defineModuleGetter(this, "NewTabUtils",
  "resource://gre/modules/NewTabUtils.jsm");
ChromeUtils.defineModuleGetter(this, "ProfileAge",
  "resource://gre/modules/ProfileAge.jsm");
ChromeUtils.defineModuleGetter(this, "ShellService",
  "resource:///modules/ShellService.jsm");

const FXA_USERNAME_PREF = "services.sync.username";
const ONBOARDING_EXPERIMENT_PREF = "browser.newtabpage.activity-stream.asrouterOnboardingCohort";
const MOZ_JEXL_FILEPATH = "mozjexl";

// Max possible cap for any message
const MAX_LIFETIME_CAP = 100;
const ONE_DAY = 24 * 60 * 60 * 1000;

const {activityStreamProvider: asProvider} = NewTabUtils;

const FRECENT_SITES_UPDATE_INTERVAL = 6 * 60 * 60 * 1000; // Six hours
const FRECENT_SITES_IGNORE_BLOCKED = true;
const FRECENT_SITES_NUM_ITEMS = 50;
const FRECENT_SITES_MIN_FRECENCY = 100;

const TopFrecentSitesCache = {
  _lastUpdated: 0,
  _topFrecentSites: null,
  get topFrecentSites() {
    return new Promise(async resolve => {
      const now = Date.now();
      if (now - this._lastUpdated >= FRECENT_SITES_UPDATE_INTERVAL) {
        this._topFrecentSites = await asProvider.getTopFrecentSites({
          ignoreBlocked: FRECENT_SITES_IGNORE_BLOCKED,
          numItems: FRECENT_SITES_NUM_ITEMS,
          topsiteFrecency: FRECENT_SITES_MIN_FRECENCY,
          onePerDomain: true,
          includeFavicon: false
        });
        this._lastUpdated = now;
      }
      resolve(this._topFrecentSites);
    });
  },
  // For testing
  expire() {
    this._lastUpdated = 0;
    this._topFrecentSites = null;
  }
};

/**
 * removeRandomItemFromArray - Removes a random item from the array and returns it.
 *
 * @param {Array} arr An array of items
 * @returns one of the items in the array
 */
function removeRandomItemFromArray(arr) {
  return arr.splice(Math.floor(Math.random() * arr.length), 1)[0];
}

const TargetingGetters = {
  get profileAgeCreated() {
    return new ProfileAge(null, null).created;
  },
  get profileAgeReset() {
    return new ProfileAge(null, null).reset;
  },
  get hasFxAccount() {
    return Services.prefs.prefHasUserValue(FXA_USERNAME_PREF);
  },
  get addonsInfo() {
    return AddonManager.getActiveAddons(["extension", "service"])
      .then(({addons, fullData}) => {
        const info = {};
        for (const addon of addons) {
          info[addon.id] = {
            version: addon.version,
            type: addon.type,
            isSystem: addon.isSystem,
            isWebExtension: addon.isWebExtension
          };
          if (fullData) {
            Object.assign(info[addon.id], {
              name: addon.name,
              userDisabled: addon.userDisabled,
              installDate: addon.installDate
            });
          }
        }
        return {addons: info, isFullData: fullData};
      });
  },

  get searchEngines() {
    return new Promise(resolve => {
      // Note: calling init ensures this code is only executed after Search has been initialized
      Services.search.init(rv => {
        if (Components.isSuccessCode(rv)) {
          let engines = Services.search.getVisibleEngines();
          resolve({
            current: Services.search.defaultEngine.identifier,
            installed: engines
              .map(engine => engine.identifier)
              .filter(engine => engine)
          });
        } else {
          resolve({installed: [], current: ""});
        }
      });
    });
  },

  get isDefaultBrowser() {
    try {
      return ShellService.isDefaultBrowser();
    } catch (e) {}
    return null;
  },

  get devToolsOpenedCount() {
    return Services.prefs.getIntPref("devtools.selfxss.count");
  },

  get topFrecentSites() {
    return TopFrecentSitesCache.topFrecentSites.then(sites => sites.map(site => (
      {
        url: site.url,
        host: (new URL(site.url)).hostname,
        frecency: site.frecency,
        lastVisitDate: site.lastVisitDate
      }
    )));
  },

  // Temporary targeting function for the purposes of running the simplified onboarding experience
  get isInExperimentCohort() {
    return Services.prefs.getIntPref(ONBOARDING_EXPERIMENT_PREF, 0);
  }
};

this.ASRouterTargeting = {
  Environment: TargetingGetters,

  ERROR_TYPES: {
    MALFORMED_EXPRESSION: "MALFORMED_EXPRESSION",
    OTHER_ERROR: "OTHER_ERROR"
  },

  isMatch(filterExpression, context = this.Environment) {
    return FilterExpressions.eval(filterExpression, context);
  },

  isTriggerMatch(trigger = {}, candidateMessageTrigger = {}) {
    if (trigger.id !== candidateMessageTrigger.id) {
      return false;
    } else if (!candidateMessageTrigger.params) {
      return true;
    }
    return candidateMessageTrigger.params.includes(trigger.param);
  },

  isBelowFrequencyCap(message, impressionsForMessage) {
    if (!message.frequency || !impressionsForMessage || !impressionsForMessage.length) {
      return true;
    }

    if (
      message.frequency.lifetime &&
      impressionsForMessage.length >= Math.min(message.frequency.lifetime, MAX_LIFETIME_CAP)
    ) {
      return false;
    }

    if (message.frequency.custom) {
      const now = Date.now();
      for (const setting of message.frequency.custom) {
        let {period} = setting;
        if (period === "daily") {
          period = ONE_DAY;
        }
        const impressionsInPeriod = impressionsForMessage.filter(t => (now - t) < period);
        if (impressionsInPeriod.length >= setting.cap) {
          return false;
        }
      }
    }

    return true;
  },

  /**
   * checkMessageTargeting - Checks is a message's targeting parameters are satisfied
   *
   * @param {*} message An AS router message
   * @param {obj} context A FilterExpression context
   * @param {func} onError A function to handle errors (takes two params; error, message)
   * @returns
   */
  async checkMessageTargeting(message, context, onError) {
    // If no targeting is specified,
    if (!message.targeting) {
      return true;
    }
    let result;
    try {
      result = await this.isMatch(message.targeting, context);
    } catch (error) {
      Cu.reportError(error);
      if (onError) {
        const type = error.fileName.includes(MOZ_JEXL_FILEPATH) ? this.ERROR_TYPES.MALFORMED_EXPRESSION : this.ERROR_TYPES.OTHER_ERROR;
        onError(type, error, message);
      }
      result = false;
    }
    return result;
  },

  /**
   * findMatchingMessage - Given an array of messages, returns one message
   *                       whos targeting expression evaluates to true
   *
   * @param {Array} messages An array of AS router messages
   * @param {obj} impressions An object containing impressions, where keys are message ids
   * @param {trigger} string A trigger expression if a message for that trigger is desired
   * @param {obj|null} context A FilterExpression context. Defaults to TargetingGetters above.
   * @returns {obj} an AS router message
   */
  async findMatchingMessage({messages, impressions = {}, trigger, context, onError}) {
    const arrayOfItems = [...messages];
    let match;
    let candidate;

    while (!match && arrayOfItems.length) {
      candidate = removeRandomItemFromArray(arrayOfItems);

      if (
        candidate &&
        (trigger ? this.isTriggerMatch(trigger, candidate.trigger) : !candidate.trigger) &&
        this.isBelowFrequencyCap(candidate, impressions[candidate.id]) &&
        // If a trigger expression was passed to this function, the message should match it.
        // Otherwise, we should choose a message with no trigger property (i.e. a message that can show up at any time)
        await this.checkMessageTargeting(candidate, context, onError)
      ) {
        match = candidate;
      }
    }
    return match;
  }
};

// Export for testing
this.TopFrecentSitesCache = TopFrecentSitesCache;
this.EXPORTED_SYMBOLS = ["ASRouterTargeting", "TopFrecentSitesCache"];
