/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Get annotation service
try {
  var annosvc = Cc["@mozilla.org/browser/annotation-service;1"].getService(Ci.nsIAnnotationService);
} catch (ex) {
  do_throw("Could not get annotation service\n");
}

add_task(async function test_execute() {
  let testURI = uri("http://mozilla.com/");
  let testItem = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.menuGuid,
    title: "",
    url: testURI,
  });
  let testItemId = await PlacesUtils.promiseItemId(testItem.guid);
  let testAnnoName = "moz-test-places/annotations";
  let testAnnoVal = "test";
  let earlierDate = new Date(Date.now() - 1000);

  // string item-annotation
  let item = await PlacesUtils.bookmarks.fetch(testItem.guid);

  // Verify that lastModified equals dateAdded before we set the annotation.
  Assert.equal(item.lastModified.getTime(), item.dateAdded.getTime());
  // Workaround possible VM timers issues moving last modified to the past.
  await PlacesUtils.bookmarks.update({
    guid: item.guid,
    dateAdded: earlierDate,
    lastModified: earlierDate,
  });

  try {
    annosvc.setItemAnnotation(testItemId, testAnnoName, testAnnoVal, 0, annosvc.EXPIRE_NEVER);
  } catch (ex) {
    do_throw("unable to add item annotation " + ex);
  }

  let updatedItem = await PlacesUtils.bookmarks.fetch(testItem.guid);

  // verify that setting the annotation updates the last modified time
  Assert.ok(updatedItem.lastModified > item.lastModified);

  try {
    var annoVal = annosvc.getItemAnnotation(testItemId, testAnnoName);
    // verify the anno value
    Assert.ok(testAnnoVal === annoVal);
  } catch (ex) {
    do_throw("unable to get item annotation");
  }

  // get annotation that doesn't exist
  try {
    annosvc.getPageAnnotation(testURI, "blah");
    do_throw("fetching page-annotation that doesn't exist, should've thrown");
  } catch (ex) {}
  try {
    annosvc.getItemAnnotation(testURI, "blah");
    do_throw("fetching item-annotation that doesn't exist, should've thrown");
  } catch (ex) {}

  // get annotation info
  var value = {}, flags = {}, exp = {}, storageType = {};
  annosvc.getItemAnnotationInfo(testItemId, testAnnoName, value, flags, exp, storageType);
  Assert.equal(value.value, testAnnoVal);
  Assert.equal(storageType.value, Ci.nsIAnnotationService.TYPE_STRING);

  // get annotation names for an item
  let annoNames = annosvc.getItemAnnotationNames(testItemId);
  Assert.equal(annoNames.length, 1);
  Assert.equal(annoNames[0], "moz-test-places/annotations");

  // test int32 anno type
  var int32Key = testAnnoName + "/types/Int32";
  var int32Val = 23;
  annosvc.setItemAnnotation(testItemId, int32Key, int32Val, 0, annosvc.EXPIRE_NEVER);
  Assert.ok(annosvc.itemHasAnnotation(testItemId, int32Key));
  annosvc.getItemAnnotationInfo(testItemId, int32Key, value, flags, exp, storageType);
  Assert.equal(value.value, int32Val);
  let storedVal = annosvc.getItemAnnotation(testItemId, int32Key);
  Assert.ok(int32Val === storedVal);

  // test int64 anno type
  var int64Key = testAnnoName + "/types/Int64";
  var int64Val = 4294967296;
  annosvc.setItemAnnotation(testItemId, int64Key, int64Val, 0, annosvc.EXPIRE_NEVER);
  Assert.ok(annosvc.itemHasAnnotation(testItemId, int64Key));
  annosvc.getItemAnnotationInfo(testItemId, int64Key, value, flags, exp, storageType);
  Assert.equal(value.value, int64Val);
  storedVal = annosvc.getItemAnnotation(testItemId, int64Key);
  Assert.ok(int64Val === storedVal);

  // test double anno type
  var doubleKey = testAnnoName + "/types/Double";
  var doubleVal = 0.000002342;
  annosvc.setItemAnnotation(testItemId, doubleKey, doubleVal, 0, annosvc.EXPIRE_NEVER);
  Assert.ok(annosvc.itemHasAnnotation(testItemId, doubleKey));
  annosvc.getItemAnnotationInfo(testItemId, doubleKey, value, flags, exp, storageType);
  Assert.equal(value.value, doubleVal);
  Assert.equal(storageType.value, Ci.nsIAnnotationService.TYPE_DOUBLE);
  storedVal = annosvc.getItemAnnotation(testItemId, doubleKey);
  Assert.ok(doubleVal === storedVal);

  // test annotation removal
  annosvc.setItemAnnotation(testItemId, testAnnoName, testAnnoVal, 0, annosvc.EXPIRE_NEVER);
  // verify that removing an annotation updates the last modified date
  testItem = await PlacesUtils.bookmarks.fetch(testItem.guid);

  var lastModified3 = testItem.lastModified;
  // Workaround possible VM timers issues moving last modified to the past.
  await PlacesUtils.bookmarks.update({
    guid: testItem.guid,
    dateAdded: earlierDate,
    lastModified: earlierDate,
  });
  annosvc.removeItemAnnotation(testItemId, int32Key);

  testItem = await PlacesUtils.bookmarks.fetch(testItem.guid);
  var lastModified4 = testItem.lastModified;
  info("verify that removing an annotation updates the last modified date");
  info("lastModified3 = " + lastModified3);
  info("lastModified4 = " + lastModified4);
  Assert.ok(is_time_ordered(lastModified3, lastModified4));

  // test that getItems/PagesWithAnnotation returns an empty array after
  // removing all items/pages which had the annotation set, see bug 380317.
  Assert.equal((await getItemsWithAnnotation(int32Key)).length, 0);
  Assert.equal((await getPagesWithAnnotation(int32Key)).length, 0);

  // Setting item annotations on invalid item ids should throw
  var invalidIds = [-1, 0, 37643];
  for (var id of invalidIds) {
    try {
      annosvc.setItemAnnotation(id, "foo", "bar", 0, annosvc.EXPIRE_NEVER);
      do_throw("setItemAnnotation* should throw for invalid item id: " + id);
    } catch (ex) { }
  }

  // setting an annotation with EXPIRE_HISTORY for an item should throw
  item = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.menuGuid,
    title: "",
    url: testURI,
  });
});

add_task(async function test_getAnnotationsHavingName() {
  let url = uri("http://cat.mozilla.org");
  let bookmark = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    title: "cat",
    url,
  });
  let id = await PlacesUtils.promiseItemId(bookmark.guid);

  let folder = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    title: "pillow",
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
  });
  let fid = await PlacesUtils.promiseItemId(folder.guid);

  const ANNOS = {
    "int": 7,
    "double": 7.7,
    "string": "seven"
  };
  for (let name in ANNOS) {
    PlacesUtils.annotations.setItemAnnotation(
      id, name, ANNOS[name], 0,
      PlacesUtils.annotations.EXPIRE_NEVER);
    PlacesUtils.annotations.setItemAnnotation(
      fid, name, ANNOS[name], 0,
      PlacesUtils.annotations.EXPIRE_NEVER);
  }

  for (let name in ANNOS) {
    let results = PlacesUtils.annotations.getAnnotationsWithName(name);
    Assert.equal(results.length, 2);

    for (let result of results) {
      Assert.equal(result.annotationName, name);
      Assert.equal(result.annotationValue, ANNOS[name]);
      if (result.uri)
        Assert.ok(result.uri.equals(url));
      else
        Assert.ok(result.itemId > 0);

      if (result.itemId != -1) {
        if (result.uri)
          Assert.equal(result.itemId, id);
        else
          Assert.equal(result.itemId, fid);
        do_check_guid_for_bookmark(result.itemId, result.guid);
      } else {
        do_check_guid_for_uri(result.uri, result.guid);
      }
    }
  }
});
