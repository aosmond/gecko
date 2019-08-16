/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{BorderRadius, BoxShadowClipMode, ClipMode, ColorF, ColorU};
use api::MAX_BLUR_RADIUS;
use api::units::*;
use crate::border::{BorderRadiusAu};
use crate::box_shadow::{
    BLUR_SAMPLE_SCALE,
    adjust_border_radius_for_box_shadow,
};
use crate::clip::{ClipChainId, ClipItemKey, ClipDataStore, ClipStore};
use crate::display_list_flattener::{IsVisible};
use crate::gpu_cache::GpuDataRequest;
use crate::intern;
use crate::internal_types::LayoutPrimitiveInfo;
use crate::frame_builder::FrameBuildingState;
use crate::prim_store::{
    PrimKey, PrimKeyCommonData, PrimitiveOpacity,
    InternablePrimitive, PrimitiveSceneData, PrimitiveStore,
    PrimTemplate, PrimTemplateCommonData, SpaceMapper,
    get_snapped_rect,
};
use crate::prim_store::PrimitiveInstanceKind;
use crate::prim_store::{OpacityBindingIndex, SegmentInstanceIndex};
use crate::util::RectHelpers;

/// Identifying key for a line decoration.
#[derive(Clone, Debug, Hash, MallocSizeOf, PartialEq, Eq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BoxShadow {
    pub color: ColorU,
    pub box_offset: LayoutVector2DAu,
    pub spread_radius: Au,
    pub blur_radius: Au,
    pub border_radius: BorderRadiusAu,
    pub clip_mode: BoxShadowClipMode,
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
    pub box_offset: LayoutVector2D,
    pub spread_radius: f32,
    pub blur_radius: f32,
    pub border_radius: BorderRadius,
    pub clip_mode: BoxShadowClipMode,
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
                box_offset: LayoutVector2D::from_au(box_shadow.kind.box_offset),
                spread_radius: box_shadow.kind.spread_radius.to_f32_px(),
                blur_radius: box_shadow.kind.blur_radius.to_f32_px(),
                border_radius: box_shadow.kind.border_radius.into(),
                clip_mode: box_shadow.kind.clip_mode,
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

    pub fn snap(
        &self,
        prim_rect: LayoutRect,
        clip_chain_id: ClipChainId,
        map_to_raster: &SpaceMapper<LayoutPixel, RasterPixel>,
        device_pixel_scale: DevicePixelScale,
        clip_store: &mut ClipStore,
        clip_data_store: &mut ClipDataStore,
    ) -> Option<(LayoutRect, ClipChainId)> {
        // Inset shadows get smaller as spread radius increases.
        let (spread_amount, prim_clip_mode) = match self.kind.clip_mode {
            BoxShadowClipMode::Outset => (self.kind.spread_radius, ClipMode::ClipOut),
            BoxShadowClipMode::Inset => (-self.kind.spread_radius, ClipMode::Clip),
        };

        // Ensure the blur radius is somewhat sensible.
        let blur_radius = f32::min(self.kind.blur_radius, MAX_BLUR_RADIUS);

        // Adjust the border radius of the box shadow per CSS-spec.
        let shadow_radius = adjust_border_radius_for_box_shadow(self.kind.border_radius, spread_amount);

        // Apply parameters that affect where the shadow rect
        // exists in the local space of the primitive.
        let box_offset = &self.kind.box_offset;
        let shadow_rect = prim_rect
            .translate(*box_offset)
            .inflate(spread_amount, spread_amount);

        let snapped_shadow_rect = get_snapped_rect(
                shadow_rect,
                map_to_raster,
                device_pixel_scale,
            ).unwrap_or(shadow_rect);

        let mut clips = Vec::with_capacity(2);

        // If blur radius is zero, we can use a fast path with
        // no blur applied.
        let final_shadow_rect = if blur_radius == 0.0 {
            // Trivial reject of box-shadows that are not visible.
            if box_offset.x == 0.0 && box_offset.y == 0.0 && spread_amount == 0.0 {
                return None;
            }

            let (final_prim_rect, clip_radius) = match self.kind.clip_mode {
                BoxShadowClipMode::Outset => {
                    if !snapped_shadow_rect.is_well_formed_and_nonempty() {
                        return None;
                    }

                    // TODO(gw): Add a fast path for ClipOut + zero border radius!
                    clips.push(
                        (
                            prim_rect.origin,
                            ClipItemKey::rounded_rect(
                                prim_rect.size,
                                self.kind.border_radius,
                                ClipMode::ClipOut,
                            ),
                        )
                    );

                    (snapped_shadow_rect, shadow_radius)
                }
                BoxShadowClipMode::Inset => {
                    if snapped_shadow_rect.is_well_formed_and_nonempty() {
                        clips.push(
                            (
                                snapped_shadow_rect.origin,
                                ClipItemKey::rounded_rect(
                                    snapped_shadow_rect.size,
                                    shadow_radius,
                                    ClipMode::ClipOut,
                                ),
                            )
                        );
                    }

                    (prim_rect, self.kind.border_radius)
                }
            };

            clips.push(
                (
                    final_prim_rect.origin,
                    ClipItemKey::rounded_rect(
                        final_prim_rect.size,
                        clip_radius,
                        ClipMode::Clip,
                    ),
                )
            );

            final_prim_rect
        } else {
            // Normal path for box-shadows with a valid blur radius.
            let blur_offset = (BLUR_SAMPLE_SCALE * blur_radius).ceil();

            // Add a normal clip mask to clip out the contents
            // of the surrounding primitive.
            clips.push(
                (
                    prim_rect.origin,
                    ClipItemKey::rounded_rect(
                        prim_rect.size,
                        self.kind.border_radius,
                        prim_clip_mode,
                    ),
                )
            );

            // Get the local rect of where the shadow will be drawn,
            // expanded to include room for the blurred region.
            let dest_rect = snapped_shadow_rect.inflate(blur_offset, blur_offset);

            // Create the box-shadow clip item.
            let shadow_clip_source = ClipItemKey::box_shadow(
                snapped_shadow_rect,
                shadow_radius,
                dest_rect.translate(LayoutVector2D::new(-prim_rect.origin.x, -prim_rect.origin.y)),
                blur_radius,
                self.kind.clip_mode,
            );

            match self.kind.clip_mode {
                BoxShadowClipMode::Outset => {
                    // Certain spread-radii make the shadow invalid.
                    if !snapped_shadow_rect.is_well_formed_and_nonempty() {
                        return None;
                    }

                    // Add the box-shadow clip source.
                    clips.push(
                        (
                            prim_rect.origin,
                            shadow_clip_source,
                        ),
                    );

                    // Outset shadows are expanded by the shadow
                    // region from the original primitive.
                    dest_rect
                }
                BoxShadowClipMode::Inset => {
                    // If the inner shadow rect contains the prim
                    // rect, no pixels will be shadowed.
                    if self.kind.border_radius.is_zero() && snapped_shadow_rect
                        .inflate(-blur_radius, -blur_radius)
                        .contains_rect(&prim_rect)
                    {
                        return None;
                    }

                    // Inset shadows are still visible, even if the
                    // inset shadow rect becomes invalid (they will
                    // just look like a solid rectangle).
                    if snapped_shadow_rect.is_well_formed_and_nonempty() {
                        clips.push(
                            (
                                prim_rect.origin,
                                shadow_clip_source,
                            ),
                        );
                    }

                    // Inset shadows draw inside the original primitive.
                    prim_rect
                }
            }
        };

        let shadow_clip_chain_id = clip_store.build_clip_chain(
            clips,
            map_to_raster.current_target_spatial_node_index,
            clip_chain_id,
            clip_data_store,
        );

        Some((final_shadow_rect, shadow_clip_chain_id))
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
