/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{BorderRadius, ClipMode, ComplexClipRegion, DeviceIntRect, DevicePixelScale, ImageMask};
use api::{ImageRendering, LayoutRect, LayoutSize, LayoutPoint, LayoutVector2D, LocalClip};
use api::{BoxShadowClipMode, LayoutToWorldScale, LineOrientation, LineStyle};
use border::{ensure_no_corner_overlap};
use box_shadow::{BLUR_SAMPLE_SCALE, BoxShadowClipSource, BoxShadowCacheKey};
use clip_scroll_tree::{ClipChainIndex, CoordinateSystemId, SpatialNodeIndex};
use ellipse::Ellipse;
use gpu_cache::{GpuCache, GpuCacheHandle, ToGpuBlocks};
use gpu_types::BoxShadowStretchMode;
use prim_store::{ClipData, ImageMaskData};
use render_task::to_cache_size;
use resource_cache::{ImageRequest, ResourceCache};
use util::{LayoutToWorldFastTransform, MaxRect, TransformedRectKind};
use util::{calculate_screen_bounding_rect, extract_inner_rect_safe, pack_as_float, recycle_vec};
use std::{iter, ops};
use std::sync::Arc;

#[derive(Debug, Copy, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipSourcesIndex(usize);

pub struct ClipStore {
    clip_sources: Vec<ClipSources>,
}

impl ClipStore {
    pub fn new() -> Self {
        ClipStore {
            clip_sources: Vec::new(),
        }
    }

    pub fn recycle(self) -> Self {
        ClipStore {
            clip_sources: recycle_vec(self.clip_sources),
        }
    }

    pub fn insert(&mut self, clip_sources: ClipSources) -> ClipSourcesIndex {
        let index = ClipSourcesIndex(self.clip_sources.len());
        self.clip_sources.push(clip_sources);
        index
    }
}

impl ops::Index<ClipSourcesIndex> for ClipStore {
    type Output = ClipSources;
    fn index(&self, index: ClipSourcesIndex) -> &Self::Output {
        &self.clip_sources[index.0]
    }
}

impl ops::IndexMut<ClipSourcesIndex> for ClipStore {
    fn index_mut(&mut self, index: ClipSourcesIndex) -> &mut Self::Output {
        &mut self.clip_sources[index.0]
    }
}

#[derive(Debug)]
pub struct LineDecorationClipSource {
    rect: LayoutRect,
    style: LineStyle,
    orientation: LineOrientation,
    wavy_line_thickness: f32,
}


pub struct ComplexTranslateIter<I> {
    source: I,
    offset: LayoutVector2D,
}

impl<I: Iterator<Item = ComplexClipRegion>> Iterator for ComplexTranslateIter<I> {
    type Item = ComplexClipRegion;
    fn next(&mut self) -> Option<Self::Item> {
        self.source
            .next()
            .map(|mut complex| {
                complex.rect = complex.rect.translate(&self.offset);
                complex
            })
    }
}

#[derive(Clone, Debug)]
pub struct ClipRegion<I> {
    pub main: LayoutRect,
    pub image_mask: Option<ImageMask>,
    pub complex_clips: I,
}

impl<J> ClipRegion<ComplexTranslateIter<J>> {
    pub fn create_for_clip_node(
        rect: LayoutRect,
        complex_clips: J,
        mut image_mask: Option<ImageMask>,
        reference_frame_relative_offset: &LayoutVector2D,
    ) -> Self
    where
        J: Iterator<Item = ComplexClipRegion>
    {
        if let Some(ref mut image_mask) = image_mask {
            image_mask.rect = image_mask.rect.translate(reference_frame_relative_offset);
        }

        ClipRegion {
            main: rect.translate(reference_frame_relative_offset),
            image_mask,
            complex_clips: ComplexTranslateIter {
                source: complex_clips,
                offset: *reference_frame_relative_offset,
            },
        }
    }
}

impl ClipRegion<Option<ComplexClipRegion>> {
    pub fn create_for_clip_node_with_local_clip(
        local_clip: &LocalClip,
        reference_frame_relative_offset: &LayoutVector2D
    ) -> Self {
        ClipRegion {
            main: local_clip
                .clip_rect()
                .translate(reference_frame_relative_offset),
            image_mask: None,
            complex_clips: match *local_clip {
                LocalClip::Rect(_) => None,
                LocalClip::RoundedRect(_, ref region) => {
                    Some(ComplexClipRegion {
                        rect: region.rect.translate(reference_frame_relative_offset),
                        radii: region.radii,
                        mode: region.mode,
                    })
                },
            }
        }
    }
}

#[derive(Debug)]
pub enum ClipSource {
    Rectangle(LayoutRect, ClipMode),
    RoundedRectangle(LayoutRect, BorderRadius, ClipMode),
    Image(ImageMask),
    BoxShadow(BoxShadowClipSource),
    LineDecoration(LineDecorationClipSource),
}

impl ClipSource {
    pub fn new_rounded_rect(
        rect: LayoutRect,
        mut radii: BorderRadius,
        clip_mode: ClipMode
    ) -> ClipSource {
        if radii.is_zero() {
            ClipSource::Rectangle(rect, clip_mode)
        } else {
            ensure_no_corner_overlap(&mut radii, &rect);
            ClipSource::RoundedRectangle(
                rect,
                radii,
                clip_mode,
            )
        }
    }

    pub fn new_line_decoration(
        rect: LayoutRect,
        style: LineStyle,
        orientation: LineOrientation,
        wavy_line_thickness: f32,
    ) -> ClipSource {
        ClipSource::LineDecoration(
            LineDecorationClipSource {
                rect,
                style,
                orientation,
                wavy_line_thickness,
            }
        )
    }

    pub fn new_box_shadow(
        shadow_rect: LayoutRect,
        shadow_radius: BorderRadius,
        prim_shadow_rect: LayoutRect,
        blur_radius: f32,
        clip_mode: BoxShadowClipMode,
    ) -> ClipSource {
        // Get the fractional offsets required to match the
        // source rect with a minimal rect.
        let fract_offset = LayoutPoint::new(
            shadow_rect.origin.x.fract().abs(),
            shadow_rect.origin.y.fract().abs(),
        );
        let fract_size = LayoutSize::new(
            shadow_rect.size.width.fract().abs(),
            shadow_rect.size.height.fract().abs(),
        );

        // Create a minimal size primitive mask to blur. In this
        // case, we ensure the size of each corner is the same,
        // to simplify the shader logic that stretches the blurred
        // result across the primitive.
        let max_corner_width = shadow_radius.top_left.width
                                    .max(shadow_radius.bottom_left.width)
                                    .max(shadow_radius.top_right.width)
                                    .max(shadow_radius.bottom_right.width);
        let max_corner_height = shadow_radius.top_left.height
                                    .max(shadow_radius.bottom_left.height)
                                    .max(shadow_radius.top_right.height)
                                    .max(shadow_radius.bottom_right.height);

        // Get maximum distance that can be affected by given blur radius.
        let blur_region = (BLUR_SAMPLE_SCALE * blur_radius).ceil();

        // If the largest corner is smaller than the blur radius, we need to ensure
        // that it's big enough that the corners don't affect the middle segments.
        let used_corner_width = max_corner_width.max(blur_region);
        let used_corner_height = max_corner_height.max(blur_region);

        // Minimal nine-patch size, corner + internal + corner.
        let min_shadow_rect_size = LayoutSize::new(
            2.0 * used_corner_width + blur_region,
            2.0 * used_corner_height + blur_region,
        );

        // The minimal rect to blur.
        let mut minimal_shadow_rect = LayoutRect::new(
            LayoutPoint::new(
                blur_region + fract_offset.x,
                blur_region + fract_offset.y,
            ),
            LayoutSize::new(
                min_shadow_rect_size.width + fract_size.width,
                min_shadow_rect_size.height + fract_size.height,
            ),
        );

        // If the width or height ends up being bigger than the original
        // primitive shadow rect, just blur the entire rect along that
        // axis and draw that as a simple blit. This is necessary for
        // correctness, since the blur of one corner may affect the blur
        // in another corner.
        let mut stretch_mode_x = BoxShadowStretchMode::Stretch;
        if shadow_rect.size.width < minimal_shadow_rect.size.width {
            minimal_shadow_rect.size.width = shadow_rect.size.width;
            stretch_mode_x = BoxShadowStretchMode::Simple;
        }

        let mut stretch_mode_y = BoxShadowStretchMode::Stretch;
        if shadow_rect.size.height < minimal_shadow_rect.size.height {
            minimal_shadow_rect.size.height = shadow_rect.size.height;
            stretch_mode_y = BoxShadowStretchMode::Simple;
        }

        // Expand the shadow rect by enough room for the blur to take effect.
        let shadow_rect_alloc_size = LayoutSize::new(
            2.0 * blur_region + minimal_shadow_rect.size.width.ceil(),
            2.0 * blur_region + minimal_shadow_rect.size.height.ceil(),
        );

        ClipSource::BoxShadow(BoxShadowClipSource {
            shadow_rect_alloc_size,
            shadow_radius,
            prim_shadow_rect,
            blur_radius,
            clip_mode,
            stretch_mode_x,
            stretch_mode_y,
            cache_handle: None,
            cache_key: None,
            clip_data_handle: GpuCacheHandle::new(),
            minimal_shadow_rect,
        })
    }

    // Return a modified clip source that is the same as self
    // but offset in local-space by a specified amount.
    pub fn offset(&self, offset: &LayoutVector2D) -> ClipSource {
        match *self {
            ClipSource::LineDecoration(ref info) => {
                ClipSource::LineDecoration(LineDecorationClipSource {
                    rect: info.rect.translate(offset),
                    ..*info
                })
            }
            _ => {
                panic!("bug: other clip sources not expected here yet");
            }
        }
    }

    pub fn is_rect(&self) -> bool {
        match *self {
            ClipSource::Rectangle(..) => true,
            _ => false,
        }
    }

    pub fn is_image_or_line_decoration_clip(&self) -> bool {
        match *self {
            ClipSource::Image(..) | ClipSource::LineDecoration(..) => true,
            _ => false,
        }
    }
}


struct BoundsAccumulator {
    local_outer: Option<LayoutRect>,
    local_inner: Option<LayoutRect>,
    can_calculate_inner_rect: bool,
    can_calculate_outer_rect: bool,
}

impl BoundsAccumulator {
    fn new() -> Self {
        BoundsAccumulator {
            local_outer: Some(LayoutRect::max_rect()),
            local_inner: Some(LayoutRect::max_rect()),
            can_calculate_inner_rect: true,
            can_calculate_outer_rect: false,
        }
    }

    fn add(&mut self, source: &ClipSource) {
        // Depending on the complexity of the clip, we may either know the outer and/or inner
        // rect, or neither or these.  In the case of a clip-out, we currently set the mask bounds
        // to be unknown. This is conservative, but ensures correctness. In the future we can make
        // this a lot more clever with some proper region handling.
        if !self.can_calculate_inner_rect {
            return
        }

        match *source {
            ClipSource::Image(ref mask) => {
                if !mask.repeat {
                    self.can_calculate_outer_rect = true;
                    self.local_outer = self.local_outer.and_then(|r| r.intersection(&mask.rect));
                }
                self.local_inner = None;
            }
            ClipSource::Rectangle(rect, mode) => {
                // Once we encounter a clip-out, we just assume the worst
                // case clip mask size, for now.
                if mode == ClipMode::ClipOut {
                    self.can_calculate_inner_rect = false;
                    return
                }

                self.can_calculate_outer_rect = true;
                self.local_outer = self.local_outer.and_then(|r| r.intersection(&rect));
                self.local_inner = self.local_inner.and_then(|r| r.intersection(&rect));
            }
            ClipSource::RoundedRectangle(ref rect, ref radius, mode) => {
                // Once we encounter a clip-out, we just assume the worst
                // case clip mask size, for now.
                if mode == ClipMode::ClipOut {
                    self.can_calculate_inner_rect = false;
                    return
                }

                self.can_calculate_outer_rect = true;
                self.local_outer = self.local_outer.and_then(|r| r.intersection(rect));

                let inner_rect = extract_inner_rect_safe(rect, radius);
                self.local_inner = self.local_inner
                    .and_then(|r| inner_rect.and_then(|ref inner| r.intersection(inner)));
            }
            ClipSource::BoxShadow(..) |
            ClipSource::LineDecoration(..) => {
                self.can_calculate_inner_rect = false;
            }
        }
    }

    fn finish(self) -> (LayoutRect, Option<LayoutRect>) {
        (
            if self.can_calculate_inner_rect {
                self.local_inner.unwrap_or_else(LayoutRect::zero)
            } else {
                LayoutRect::zero()
            },
            if self.can_calculate_outer_rect {
                Some(self.local_outer.unwrap_or_else(LayoutRect::zero))
            } else {
                None
            },
        )
    }
}


#[derive(Debug)]
pub struct ClipSources {
    pub clips: Vec<(ClipSource, GpuCacheHandle)>,
    pub local_inner_rect: LayoutRect,
    pub local_outer_rect: Option<LayoutRect>,
    pub only_rectangular_clips: bool,
    pub has_image_or_line_decoration_clip: bool,
    pub spatial_node_index: SpatialNodeIndex,
}

impl ClipSources {
    pub fn new<I>(clip_iter: I, spatial_node_index: SpatialNodeIndex) -> Self
    where
        I: IntoIterator<Item = ClipSource>,
    {
        let mut clips = Vec::new();
        let mut bounds_accum = BoundsAccumulator::new();
        let mut has_image_or_line_decoration_clip = false;
        let mut only_rectangular_clips = true;

        for clip in clip_iter {
            bounds_accum.add(&clip);
            has_image_or_line_decoration_clip |= clip.is_image_or_line_decoration_clip();
            only_rectangular_clips &= clip.is_rect();
            clips.push((clip, GpuCacheHandle::new()));
        }

        only_rectangular_clips &= !has_image_or_line_decoration_clip;
        let (local_inner_rect, local_outer_rect) = bounds_accum.finish();

        ClipSources {
            clips,
            local_inner_rect,
            local_outer_rect,
            only_rectangular_clips,
            has_image_or_line_decoration_clip,
            spatial_node_index,
        }
    }

    pub fn from_region<I>(
        region: ClipRegion<I>,
        spatial_node_index: SpatialNodeIndex,
    ) -> ClipSources
    where
        I: IntoIterator<Item = ComplexClipRegion>
    {
        let clip_rect = iter::once(ClipSource::Rectangle(region.main, ClipMode::Clip));
        let clip_image = region.image_mask.map(ClipSource::Image);
        let clips_complex = region.complex_clips
            .into_iter()
            .map(|complex| ClipSource::new_rounded_rect(
                complex.rect,
                complex.radii,
                complex.mode,
            ));

        let clips_all = clip_rect.chain(clip_image).chain(clips_complex);
        ClipSources::new(clips_all, spatial_node_index)
    }

    pub fn clips(&self) -> &[(ClipSource, GpuCacheHandle)] {
        &self.clips
    }

    pub fn update(
        &mut self,
        gpu_cache: &mut GpuCache,
        resource_cache: &mut ResourceCache,
        device_pixel_scale: DevicePixelScale,
    ) {
        for &mut (ref mut source, ref mut handle) in &mut self.clips {
            if let Some(mut request) = gpu_cache.request(handle) {
                match *source {
                    ClipSource::Image(ref mask) => {
                        let data = ImageMaskData { local_rect: mask.rect };
                        data.write_gpu_blocks(request);
                    }
                    ClipSource::BoxShadow(ref info) => {
                        request.push([
                            info.shadow_rect_alloc_size.width,
                            info.shadow_rect_alloc_size.height,
                            info.clip_mode as i32 as f32,
                            0.0,
                        ]);
                        request.push([
                            info.stretch_mode_x as i32 as f32,
                            info.stretch_mode_y as i32 as f32,
                            0.0,
                            0.0,
                        ]);
                        request.push(info.prim_shadow_rect);
                    }
                    ClipSource::Rectangle(rect, mode) => {
                        let data = ClipData::uniform(rect, 0.0, mode);
                        data.write(&mut request);
                    }
                    ClipSource::RoundedRectangle(ref rect, ref radius, mode) => {
                        let data = ClipData::rounded_rect(rect, radius, mode);
                        data.write(&mut request);
                    }
                    ClipSource::LineDecoration(ref info) => {
                        request.push(info.rect);
                        request.push([
                            info.wavy_line_thickness,
                            pack_as_float(info.style as u32),
                            pack_as_float(info.orientation as u32),
                            0.0,
                        ]);
                    }
                }
            }

            match *source {
                ClipSource::Image(ref mask) => {
                    resource_cache.request_image(
                        ImageRequest {
                            key: mask.image,
                            rendering: ImageRendering::Auto,
                            tile: None,
                        },
                        gpu_cache,
                    );
                }
                ClipSource::BoxShadow(ref mut info) => {
                    // Quote from https://drafts.csswg.org/css-backgrounds-3/#shadow-blur
                    // "the image that would be generated by applying to the shadow a
                    // Gaussian blur with a standard deviation equal to half the blur radius."
                    let blur_radius_dp = (info.blur_radius * 0.5 * device_pixel_scale.0).round();

                    // Create the cache key for this box-shadow render task.
                    let content_scale = LayoutToWorldScale::new(1.0) * device_pixel_scale;
                    let cache_size = to_cache_size(info.shadow_rect_alloc_size * content_scale);
                    let bs_cache_key = BoxShadowCacheKey {
                        blur_radius_dp: blur_radius_dp as i32,
                        clip_mode: info.clip_mode,
                        rect_size: (info.shadow_rect_alloc_size * content_scale).round().to_i32(),
                        br_top_left: (info.shadow_radius.top_left * content_scale).round().to_i32(),
                        br_top_right: (info.shadow_radius.top_right * content_scale).round().to_i32(),
                        br_bottom_right: (info.shadow_radius.bottom_right * content_scale).round().to_i32(),
                        br_bottom_left: (info.shadow_radius.bottom_left * content_scale).round().to_i32(),
                    };

                    info.cache_key = Some((cache_size, bs_cache_key));

                    if let Some(mut request) = gpu_cache.request(&mut info.clip_data_handle) {
                        let data = ClipData::rounded_rect(
                            &info.minimal_shadow_rect,
                            &info.shadow_radius,
                            ClipMode::Clip,
                        );

                        data.write(&mut request);
                    }
                }
                _ => {}
            }
        }
    }

    pub fn get_screen_bounds(
        &self,
        transform: &LayoutToWorldFastTransform,
        device_pixel_scale: DevicePixelScale,
        screen_rect: Option<&DeviceIntRect>,
    ) -> (DeviceIntRect, Option<DeviceIntRect>) {
        // If this translation isn't axis aligned or has a perspective component, don't try to
        // calculate the inner rectangle. The rectangle that we produce would include potentially
        // clipped screen area.
        // TODO(mrobinson): We should eventually try to calculate an inner region or some inner
        // rectangle so that we can do screen inner rectangle optimizations for these kind of
        // cilps.
        let can_calculate_inner_rect =
            transform.kind() == TransformedRectKind::AxisAligned &&
            !transform.has_perspective_component();
        let screen_inner_rect = if can_calculate_inner_rect {
            calculate_screen_bounding_rect(transform, &self.local_inner_rect, device_pixel_scale, screen_rect)
                .unwrap_or(DeviceIntRect::zero())
        } else {
            DeviceIntRect::zero()
        };

        let screen_outer_rect = self.local_outer_rect.map(|outer_rect|
            calculate_screen_bounding_rect(transform, &outer_rect, device_pixel_scale, screen_rect)
                .unwrap_or(DeviceIntRect::zero())
        );

        (screen_inner_rect, screen_outer_rect)
    }
}

/// Represents a local rect and a device space
/// rectangles that are either outside or inside bounds.
#[derive(Clone, Debug, PartialEq)]
pub struct Geometry {
    pub local_rect: LayoutRect,
    pub device_rect: DeviceIntRect,
}

impl From<LayoutRect> for Geometry {
    fn from(local_rect: LayoutRect) -> Self {
        Geometry {
            local_rect,
            device_rect: DeviceIntRect::zero(),
        }
    }
}

pub fn rounded_rectangle_contains_point(
    point: &LayoutPoint,
    rect: &LayoutRect,
    radii: &BorderRadius
) -> bool {
    if !rect.contains(point) {
        return false;
    }

    let top_left_center = rect.origin + radii.top_left.to_vector();
    if top_left_center.x > point.x && top_left_center.y > point.y &&
       !Ellipse::new(radii.top_left).contains(*point - top_left_center.to_vector()) {
        return false;
    }

    let bottom_right_center = rect.bottom_right() - radii.bottom_right.to_vector();
    if bottom_right_center.x < point.x && bottom_right_center.y < point.y &&
       !Ellipse::new(radii.bottom_right).contains(*point - bottom_right_center.to_vector()) {
        return false;
    }

    let top_right_center = rect.top_right() +
                           LayoutVector2D::new(-radii.top_right.width, radii.top_right.height);
    if top_right_center.x < point.x && top_right_center.y > point.y &&
       !Ellipse::new(radii.top_right).contains(*point - top_right_center.to_vector()) {
        return false;
    }

    let bottom_left_center = rect.bottom_left() +
                             LayoutVector2D::new(radii.bottom_left.width, -radii.bottom_left.height);
    if bottom_left_center.x > point.x && bottom_left_center.y < point.y &&
       !Ellipse::new(radii.bottom_left).contains(*point - bottom_left_center.to_vector()) {
        return false;
    }

    true
}

pub type ClipChainNodeRef = Option<Arc<ClipChainNode>>;

#[derive(Debug, Clone)]
pub struct ClipChainNode {
    pub work_item: ClipWorkItem,
    pub local_clip_rect: LayoutRect,
    pub screen_outer_rect: DeviceIntRect,
    pub screen_inner_rect: DeviceIntRect,
    pub prev: ClipChainNodeRef,
}

#[derive(Debug, Clone)]
pub struct ClipChain {
    pub parent_index: Option<ClipChainIndex>,
    pub combined_outer_screen_rect: DeviceIntRect,
    pub combined_inner_screen_rect: DeviceIntRect,
    pub nodes: ClipChainNodeRef,
    pub has_non_root_coord_system: bool,
}

impl ClipChain {
    pub fn empty(screen_rect: &DeviceIntRect) -> Self {
        ClipChain {
            parent_index: None,
            combined_inner_screen_rect: *screen_rect,
            combined_outer_screen_rect: *screen_rect,
            nodes: None,
            has_non_root_coord_system: false,
        }
    }

    pub fn new_with_added_node(&self, new_node: &ClipChainNode) -> Self {
        // If the new node's inner rectangle completely surrounds our outer rectangle,
        // we can discard the new node entirely since it isn't going to affect anything.
        if new_node.screen_inner_rect.contains_rect(&self.combined_outer_screen_rect) {
            return self.clone();
        }

        let mut new_chain = self.clone();
        new_chain.add_node(new_node.clone());
        new_chain
    }

    pub fn add_node(&mut self, mut new_node: ClipChainNode) {
        new_node.prev = self.nodes.clone();

        // If this clip's outer rectangle is completely enclosed by the clip
        // chain's inner rectangle, then the only clip that matters from this point
        // on is this clip. We can disconnect this clip from the parent clip chain.
        if self.combined_inner_screen_rect.contains_rect(&new_node.screen_outer_rect) {
            new_node.prev = None;
        }

        self.combined_outer_screen_rect =
            self.combined_outer_screen_rect.intersection(&new_node.screen_outer_rect)
            .unwrap_or_else(DeviceIntRect::zero);
        self.combined_inner_screen_rect =
            self.combined_inner_screen_rect.intersection(&new_node.screen_inner_rect)
            .unwrap_or_else(DeviceIntRect::zero);

        self.has_non_root_coord_system |= new_node.work_item.coordinate_system_id != CoordinateSystemId::root();

        self.nodes = Some(Arc::new(new_node));
    }
}

pub struct ClipChainNodeIter {
    pub current: ClipChainNodeRef,
}

impl Iterator for ClipChainNodeIter {
    type Item = Arc<ClipChainNode>;

    fn next(&mut self) -> ClipChainNodeRef {
        let previous = self.current.clone();
        self.current = match self.current {
            Some(ref item) => item.prev.clone(),
            None => return None,
        };
        previous
    }
}

#[derive(Debug, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipWorkItem {
    pub clip_sources_index: ClipSourcesIndex,
    pub coordinate_system_id: CoordinateSystemId,
}

