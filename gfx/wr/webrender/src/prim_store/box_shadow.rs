/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{
    ColorF, ColorU,
};
use api::units::{LayoutVector2D};
use crate::display_list_flattener::{IsVisible};
use crate::gpu_cache::GpuDataRequest;
use crate::intern;
use crate::internal_types::LayoutPrimitiveInfo;
use crate::frame_builder::FrameBuildingState;
use crate::prim_store::{
    PrimKey, PrimKeyCommonData, PrimitiveOpacity,
    InternablePrimitive, PrimitiveSceneData, PrimitiveStore,
    PrimTemplate, PrimTemplateCommonData,
};
use crate::prim_store::PrimitiveInstanceKind;
use crate::prim_store::{OpacityBindingIndex, SegmentInstanceIndex};

/// Identifying key for a line decoration.
#[derive(Clone, Debug, Hash, MallocSizeOf, PartialEq, Eq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BoxShadow {
    pub color: ColorU,
}

pub type BoxShadowKey = PrimKey<BoxShadow>;

impl BoxShadowKey {
    pub fn new(
        info: &LayoutPrimitiveInfo,
        box_shadow: BoxShadow,
    ) -> Self {
        BoxShadowKey {
            common: PrimKeyCommonData::with_info(
                info,
            ),
            kind: box_shadow,
        }
    }
}

impl intern::InternDebug for BoxShadowKey {}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(MallocSizeOf)]
pub struct BoxShadowData {
    pub color: ColorF,
}

impl BoxShadowData {
    /// Write any GPU blocks for the primitive template to the given request object.
    pub fn write_prim_gpu_blocks(
        &self,
        request: &mut GpuDataRequest
    ) {
        request.push(self.color.premultiplied());
    }
}

pub type BoxShadowTemplate = PrimTemplate<BoxShadowData>;

impl From<BoxShadowKey> for BoxShadowTemplate {
    fn from(box_shadow: BoxShadowKey) -> Self {
        let common = PrimTemplateCommonData::with_key_common(box_shadow.common);
        BoxShadowTemplate {
            common,
            kind: BoxShadowData {
                color: box_shadow.kind.color.into(),
            }
        }
    }
}

impl BoxShadowTemplate {
    /// Update the GPU cache for a given primitive template. This may be called multiple
    /// times per frame, by each primitive reference that refers to this interned
    /// template. The initial request call to the GPU cache ensures that work is only
    /// done if the cache entry is invalid (due to first use or eviction).
    pub fn update(
        &mut self,
        frame_state: &mut FrameBuildingState,
    ) {
        if let Some(mut request) = frame_state.gpu_cache.request(&mut self.common.gpu_cache_handle) {
            self.kind.write_prim_gpu_blocks(&mut request);
        }

        self.common.opacity = PrimitiveOpacity::from_alpha(self.kind.color.a);
    }
}

pub type BoxShadowDataHandle = intern::Handle<BoxShadow>;

impl intern::Internable for BoxShadow {
    type Key = BoxShadowKey;
    type StoreData = BoxShadowTemplate;
    type InternData = PrimitiveSceneData;
}

impl InternablePrimitive for BoxShadow {
    fn into_key(
        self,
        info: &LayoutPrimitiveInfo,
    ) -> BoxShadowKey {
        BoxShadowKey::new(
            info,
            self,
        )
    }

    fn make_instance_kind(
        _key: BoxShadowKey,
        data_handle: BoxShadowDataHandle,
        _: &mut PrimitiveStore,
        _reference_frame_relative_offset: LayoutVector2D,
    ) -> PrimitiveInstanceKind {
        PrimitiveInstanceKind::BoxShadow {
            data_handle,
            opacity_binding_index: OpacityBindingIndex::INVALID,
            segment_instance_index: SegmentInstanceIndex::INVALID,
        }
    }
}

impl IsVisible for BoxShadow {
    fn is_visible(&self) -> bool {
        self.color.a > 0
    }
}

#[test]
#[cfg(target_pointer_width = "64")]
fn test_struct_sizes() {
    use std::mem;
    // The sizes of these structures are critical for performance on a number of
    // talos stress tests. If you get a failure here on CI, there's two possibilities:
    // (a) You made a structure smaller than it currently is. Great work! Update the
    //     test expectations and move on.
    // (b) You made a structure larger. This is not necessarily a problem, but should only
    //     be done with care, and after checking if talos performance regresses badly.
    assert_eq!(mem::size_of::<BoxShadow>(), 20, "BoxShadow size changed");
    assert_eq!(mem::size_of::<BoxShadowTemplate>(), 52, "BoxShadowTemplate size changed");
    assert_eq!(mem::size_of::<BoxShadowKey>(), 32, "BoxShadowKey size changed");
}
