## Activity Stream Router message format

Field name | Type     | Required | Description | Example / Note
---        | ---      | ---      | ---         | ---
`id`       | `string` | Yes | A unique identifier for the message that should not conflict with any other previous message | `ONBOARDING_1`
`template` | `string` | Yes | An id matching an existing Activity Stream Router template | [See example](https://github.com/mozilla/activity-stream/blob/33669c67c2269078a6d3d6d324fb48175d98f634/system-addon/content-src/message-center/templates/SimpleSnippet.jsx)
`publish_start` | `date` | No | When to start showing the message | `1524474850876`
`publish_end` | `date` | No | When to stop showing the message | `1524474850876`
`content` | `object` | Yes | An object containing all variables/props to be rendered in the template. Subset of allowed tags detailed below. | [See example below](#html-subset)
`bundled` | `integer` | No | The number of messages of the same template this one should be shown with | [See example below](#a-bundled-message-example)
`order` | `integer` | No | If bundled with other messages of the same template, which order should this one be placed in? Defaults to 0 if no order is desired | [See example below](#a-bundled-message-example)
`campaign` | `string` | No | Campaign id that the message belongs to | `RustWebAssembly`
`targeting` | `string` `JEXL` | No | A [JEXL expression](http://normandy.readthedocs.io/en/latest/user/filter_expressions.html#jexl-basics) with all targeting information needed in order to decide if the message is shown | Not yet implemented, [Examples](#targeting-attributes)
`trigger` | `string` | No | An event or condition upon which the message will be immediately shown. This can be combined with `targeting`. Messages that define a trigger will not be shown during non-trigger-based passive message rotation.
`frequency` | `object` | No | A definition for frequency cap information for the message
`frequency.lifetime` | `integer` | No | The maximum number of lifetime impressions for the message.
`frequency.custom` | `array` | No | An array of frequency cap definition objects including `period`, a time period in milliseconds, and `cap`, a max number of impressions for that period.

### Message example
```javascript
{
  id: "ONBOARDING_1",
  template: "simple_snippet",
  content: {
    title: "Find it faster",
    body: "Access all of your favorite search engines with a click. Search the whole Web or just one website from the search box."
  },
  targeting: "hasFxAccount && !addonsInfo.addons['activity-stream@mozilla.org']",
  frequency: {
    lifetime: 20,
    custom: [{period: "daily", cap: 5}, {period: 3600000, cap: 1}]
  }
}
```

### A Bundled Message example
The following 2 messages have a `bundled` property, indicating that they should be shown together, since they have the same template. The number `2` indicates that this message should be shown in a bundle of 2 messages of the same template. The order property defines that ONBOARDING_2 should be shown after ONBOARDING_3 in the bundle.
```javascript
{
  id: "ONBOARDING_2",
  template: "onboarding",
  bundled: 2,
  order: 2,
  content: {
    title: "Private Browsing",
    body: "Browse by yourself. Private Browsing with Tracking Protection blocks online trackers that follow you around the web."
  },
  targeting: "",
  trigger: "firstRun"
}
{
  id: "ONBOARDING_3",
  template: "onboarding",
  bundled: 2,
  order: 1,
  content: {
    title: "Find it faster",
    body: "Access all of your favorite search engines with a click. Search the whole Web or just one website from the search box."
  },
  targeting: "",
  trigger: "firstRun"
}
```

### HTML subset
The following tags are allowed in the content of the snippet: `i, b, u, strong, em, br`.

Links cannot be rendered using regular anchor tags because [Fluent does not allow for href attributes](https://github.com/projectfluent/fluent.js/blob/a03d3aa833660f8c620738b26c80e46b1a4edb05/fluent-dom/src/overlay.js#L13). They will be wrapped in custom tags, for example `<cta>link</cta>` and the url will be provided as part of the payload:
```
{
  "id": "7899",
  "content": {
    "text": "Use the CMD (CTRL) + T keyboard shortcut to <cta>open a new tab quickly!</cta>",
    "links": {
      "cta": {
        "url": "https://support.mozilla.org/en-US/kb/keyboard-shortcuts-perform-firefox-tasks-quickly"
      }
    }
  }
}
```
If a tag that is not on the allowed is used, the text content will be extracted and displayed.

Grouping multiple allowed elements is not possible, only the first level will be used: `<u><b>text</b></u>` will be interpreted as `<u>text</u>`.

### Targeting attributes
For a more in-depth explanation of JEXL syntax you can read the [Normady project docs](https://normandy.readthedocs.io/en/stable/user/filters.html#jexl-basics).

Currently we expose the following targeting attributes that can be used by messages:

Name | Type | Example value | Description
---  | ---  | ---           | ---
`addonsInfo` | `Object` | [example below](#addonsinfo-example) | Information about the addons the user has installed
`devToolsOpenedCount` | `Integer` | Number of usages of the web console or scratchpad
`hasFxAccount` | `Boolean` | `true` | Does the user have a firefox account
`isDefaultBrowser` | `Boolean` or `null` | Is Firefox the user's default browser? If we could not determine the default browser, this value is `null`
`profileAgeCreated` | Number | `1522843725924` | Profile creation timestamp
`profileAgeReset` | `Number` or `undefined` | `1522843725924` | When (if) the profile was reset
`searchEngines` | `Object` | [example below](#searchengines-example) | Information about the current and available search engines

#### addonsInfo Example

```javascript
{
  "addons": {
    ...
    "activity-stream@mozilla.org": {
      "version": "2018.07.06.1113-783442c0",
      "type": "extension",
      "isSystem": true,
      "isWebExtension": false,
      "name": "Activity Stream",
      "userDisabled": false,
      "installDate": "2018-03-10T03:41:06.000Z"
    }
  },
  "isFullData": true
}
```

#### searchEngines Example

```javascript
{
  "searchEngines": {
    "current": "google",
    "installed": ["google", "amazondotcom", "duckduckgo"]
  }
}
```

#### Usage
A message needs to contain the `targeting` property (JEXL string) which is evaluated against the provided attributes.
Examples:

```javascript
{
  "id": "7864",
  "content": {...},
  // simple equality check
  "targeting": "hasFxAccount == true"
}

{
  "id": "7865",
  "content": {...},
  // using JEXL transforms and combining two attributes
  "targeting": "hasFxAccount == true && profileAgeCreated > '2018-01-07'|date"
}

{
  "id": "7866",
  "content": {...},
  // targeting addon information
  "targeting": "addonsInfo.addons['activity-stream@mozilla.org'].name == 'Activity Stream'"
}
```
