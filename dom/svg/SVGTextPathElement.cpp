/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGTextPathElement.h"
#include "mozilla/dom/SVGLengthBinding.h"
#include "mozilla/dom/SVGTextContentElementBinding.h"
#include "mozilla/dom/SVGTextPathElementBinding.h"
#include "nsSVGElement.h"
#include "nsGkAtoms.h"
#include "nsError.h"

NS_IMPL_NS_NEW_NAMESPACED_SVG_ELEMENT(TextPath)

namespace mozilla {
namespace dom {

using namespace SVGTextContentElement_Binding;
using namespace SVGTextPathElement_Binding;

class SVGAnimatedLength;

JSObject*
SVGTextPathElement::WrapNode(JSContext *aCx, JS::Handle<JSObject*> aGivenProto)
{
  return SVGTextPathElement_Binding::Wrap(aCx, this, aGivenProto);
}

nsSVGElement::LengthInfo SVGTextPathElement::sLengthInfo[2] =
{
  // from SVGTextContentElement:
  { &nsGkAtoms::textLength, 0, SVGLength_Binding::SVG_LENGTHTYPE_NUMBER, SVGContentUtils::XY },
  // from SVGTextPathElement:
  { &nsGkAtoms::startOffset, 0, SVGLength_Binding::SVG_LENGTHTYPE_NUMBER, SVGContentUtils::X }
};

nsSVGEnumMapping SVGTextPathElement::sMethodMap[] = {
  {&nsGkAtoms::align, TEXTPATH_METHODTYPE_ALIGN},
  {&nsGkAtoms::stretch, TEXTPATH_METHODTYPE_STRETCH},
  {nullptr, 0}
};

nsSVGEnumMapping SVGTextPathElement::sSpacingMap[] = {
  {&nsGkAtoms::_auto, TEXTPATH_SPACINGTYPE_AUTO},
  {&nsGkAtoms::exact, TEXTPATH_SPACINGTYPE_EXACT},
  {nullptr, 0}
};

nsSVGEnumMapping SVGTextPathElement::sSideMap[] = {
  {&nsGkAtoms::left, TEXTPATH_SIDETYPE_LEFT},
  {&nsGkAtoms::right, TEXTPATH_SIDETYPE_RIGHT},
  {nullptr, 0}
};

nsSVGElement::EnumInfo SVGTextPathElement::sEnumInfo[4] =
{
  // from SVGTextContentElement:
  { &nsGkAtoms::lengthAdjust,
    sLengthAdjustMap,
    LENGTHADJUST_SPACING
  },
  // from SVGTextPathElement:
  { &nsGkAtoms::method,
    sMethodMap,
    TEXTPATH_METHODTYPE_ALIGN
  },
  { &nsGkAtoms::spacing,
    sSpacingMap,
    TEXTPATH_SPACINGTYPE_EXACT
  },
  { &nsGkAtoms::side_,
    sSideMap,
    TEXTPATH_SIDETYPE_LEFT
  }
};

nsSVGElement::StringInfo SVGTextPathElement::sStringInfo[2] =
{
  { &nsGkAtoms::href, kNameSpaceID_None, true },
  { &nsGkAtoms::href, kNameSpaceID_XLink, true }
};

//----------------------------------------------------------------------
// Implementation

SVGTextPathElement::SVGTextPathElement(already_AddRefed<mozilla::dom::NodeInfo>& aNodeInfo)
  : SVGTextPathElementBase(aNodeInfo)
{
}

//----------------------------------------------------------------------
// nsINode methods

NS_IMPL_ELEMENT_CLONE_WITH_INIT(SVGTextPathElement)

already_AddRefed<SVGAnimatedString>
SVGTextPathElement::Href()
{
  return mStringAttributes[HREF].IsExplicitlySet()
         ? mStringAttributes[HREF].ToDOMAnimatedString(this)
         : mStringAttributes[XLINK_HREF].ToDOMAnimatedString(this);
}

//----------------------------------------------------------------------

already_AddRefed<SVGAnimatedLength>
SVGTextPathElement::StartOffset()
{
  return mLengthAttributes[STARTOFFSET].ToDOMAnimatedLength(this);
}

already_AddRefed<SVGAnimatedEnumeration>
SVGTextPathElement::Method()
{
  return mEnumAttributes[METHOD].ToDOMAnimatedEnum(this);
}

already_AddRefed<SVGAnimatedEnumeration>
SVGTextPathElement::Spacing()
{
  return mEnumAttributes[SPACING].ToDOMAnimatedEnum(this);
}

already_AddRefed<SVGAnimatedEnumeration>
SVGTextPathElement::Side()
{
  return mEnumAttributes[SIDE].ToDOMAnimatedEnum(this);
}

//----------------------------------------------------------------------
// nsIContent methods

NS_IMETHODIMP_(bool)
SVGTextPathElement::IsAttributeMapped(const nsAtom* name) const
{
  static const MappedAttributeEntry* const map[] = {
    sColorMap,
    sFillStrokeMap,
    sFontSpecificationMap,
    sGraphicsMap,
    sTextContentElementsMap
  };

  return FindAttributeDependence(name, map) ||
    SVGTextPathElementBase::IsAttributeMapped(name);
}

//----------------------------------------------------------------------
// nsSVGElement overrides

nsSVGElement::LengthAttributesInfo
SVGTextPathElement::GetLengthInfo()
{
  return LengthAttributesInfo(mLengthAttributes, sLengthInfo,
                              ArrayLength(sLengthInfo));
}

nsSVGElement::EnumAttributesInfo
SVGTextPathElement::GetEnumInfo()
{
  return EnumAttributesInfo(mEnumAttributes, sEnumInfo,
                            ArrayLength(sEnumInfo));
}

nsSVGElement::StringAttributesInfo
SVGTextPathElement::GetStringInfo()
{
  return StringAttributesInfo(mStringAttributes, sStringInfo,
                              ArrayLength(sStringInfo));
}

} // namespace dom
} // namespace mozilla
