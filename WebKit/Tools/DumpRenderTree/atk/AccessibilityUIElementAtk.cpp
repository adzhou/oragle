/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
 * Copyright (C) 2009 Jan Michael Alonzo
 * Copyright (C) 2013 Samsung Electronics. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "AccessibilityUIElement.h"

#if HAVE(ACCESSIBILITY)

#include "AccessibilityNotificationHandlerAtk.h"
#include <JavaScriptCore/JSStringRef.h>
#include <JavaScriptCore/OpaqueJSString.h>
#include <atk/atk.h>
#include <wtf/Assertions.h>
#include <wtf/gobject/GRefPtr.h>
#include <wtf/gobject/GUniquePtr.h>
#include <wtf/text/CString.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/text/WTFString.h>
#include <wtf/unicode/CharacterNames.h>

namespace {

enum AtkAttributeType {
    ObjectAttributeType,
    TextAttributeType
};

enum AttributeDomain {
    CoreDomain = 0,
    AtkDomain
};

enum AttributesIndex {
    // Attribute names.
    InvalidNameIndex = 0,
    PlaceholderNameIndex,
    SortNameIndex,

    // Attribute values.
    SortAscendingValueIndex,
    SortDescendingValueIndex,
    SortUnknownValueIndex,

    NumberOfAttributes
};

// Attribute names & Values (keep on sync with enum AttributesIndex).
const String attributesMap[][2] = {
    // Attribute names.
    { "AXInvalid", "invalid" },
    { "AXPlaceholderValue", "placeholder-text" } ,
    { "AXSortDirection", "sort" },

    // Attribute values.
    { "AXAscendingSortDirection", "ascending" },
    { "AXDescendingSortDirection", "descending" },
    { "AXUnknownSortDirection", "unknown" }
};

#if ATK_CHECK_VERSION(2, 11, 3)
const char* landmarkStringBanner = "AXLandmarkBanner";
const char* landmarkStringComplementary = "AXLandmarkComplementary";
const char* landmarkStringContentinfo = "AXLandmarkContentInfo";
const char* landmarkStringMain = "AXLandmarkMain";
const char* landmarkStringNavigation = "AXLandmarkNavigation";
const char* landmarkStringSearch = "AXLandmarkSearch";
#endif

String jsStringToWTFString(JSStringRef attribute)
{
    size_t bufferSize = JSStringGetMaximumUTF8CStringSize(attribute);
    GUniquePtr<gchar> buffer(static_cast<gchar*>(g_malloc(bufferSize)));
    JSStringGetUTF8CString(attribute, buffer.get(), bufferSize);

    return String::fromUTF8(buffer.get());
}

String coreAttributeToAtkAttribute(JSStringRef attribute)
{
    String attributeString = jsStringToWTFString(attribute);
    for (int i = 0; i < NumberOfAttributes; ++i) {
        if (attributesMap[i][CoreDomain] == attributeString)
            return attributesMap[i][AtkDomain];
    }

    return attributeString;
}

String atkAttributeValueToCoreAttributeValue(AtkAttributeType type, const String& id, const String& value)
{
    if (type == ObjectAttributeType) {
        // We need to translate ATK values exposed for 'aria-sort' (e.g. 'ascending')
        // into those expected by the layout tests (e.g. 'AXAscendingSortDirection').
        if (id == attributesMap[SortNameIndex][AtkDomain] && !value.isEmpty()) {
            if (value == attributesMap[SortAscendingValueIndex][AtkDomain])
                return attributesMap[SortAscendingValueIndex][CoreDomain];
            if (value == attributesMap[SortDescendingValueIndex][AtkDomain])
                return attributesMap[SortDescendingValueIndex][CoreDomain];

            return attributesMap[SortUnknownValueIndex][CoreDomain];
        }
    } else if (type == TextAttributeType) {
        // In case of 'aria-invalid' when the attribute empty or has "false" for ATK
        // it should not be mapped at all, but layout tests will expect 'false'.
        if (id == attributesMap[InvalidNameIndex][AtkDomain] && value.isEmpty())
            return "false";
    }

    return value;
}

AtkAttributeSet* getAttributeSet(AtkObject* accessible, AtkAttributeType type)
{
    if (type == ObjectAttributeType)
        return atk_object_get_attributes(accessible);

    if (type == TextAttributeType) {
        if (!ATK_IS_TEXT(accessible))
            return nullptr;

        return atk_text_get_default_attributes(ATK_TEXT(accessible));
    }

    ASSERT_NOT_REACHED();
    return nullptr;
}

String getAttributeSetValueForId(AtkObject* accessible, AtkAttributeType type, String id)
{
    AtkAttributeSet* attributeSet = getAttributeSet(accessible, type);
    if (!attributeSet)
        return String();

    String attributeValue;
    for (AtkAttributeSet* attributes = attributeSet; attributes; attributes = attributes->next) {
        AtkAttribute* atkAttribute = static_cast<AtkAttribute*>(attributes->data);
        if (id == atkAttribute->name) {
            attributeValue = String::fromUTF8(atkAttribute->value);
            break;
        }
    }
    atk_attribute_set_free(attributeSet);

    return atkAttributeValueToCoreAttributeValue(type, id, attributeValue);
}

String getAtkAttributeSetAsString(AtkObject* accessible, AtkAttributeType type)
{
    AtkAttributeSet* attributeSet = getAttributeSet(accessible, type);
    if (!attributeSet)
        return String();

    StringBuilder builder;
    for (AtkAttributeSet* attributes = attributeSet; attributes; attributes = attributes->next) {
        AtkAttribute* attribute = static_cast<AtkAttribute*>(attributes->data);
        GUniquePtr<gchar> attributeData(g_strconcat(attribute->name, ":", attribute->value, NULL));
        builder.append(attributeData.get());
        if (attributes->next)
            builder.append(", ");
    }
    atk_attribute_set_free(attributeSet);

    return builder.toString();
}

const char* roleToString(AtkObject* object)
{
    AtkRole role = atk_object_get_role(object);

#if ATK_CHECK_VERSION(2, 11, 3)
    if (role == ATK_ROLE_LANDMARK) {
        String xmlRolesValue = getAttributeSetValueForId(object, ObjectAttributeType, "xml-roles");
        if (equalIgnoringCase(xmlRolesValue, "banner"))
            return landmarkStringBanner;
        if (equalIgnoringCase(xmlRolesValue, "complementary"))
            return landmarkStringComplementary;
        if (equalIgnoringCase(xmlRolesValue, "contentinfo"))
            return landmarkStringContentinfo;
        if (equalIgnoringCase(xmlRolesValue, "main"))
            return landmarkStringMain;
        if (equalIgnoringCase(xmlRolesValue, "navigation"))
            return landmarkStringNavigation;
        if (equalIgnoringCase(xmlRolesValue, "search"))
            return landmarkStringSearch;
    }
#endif

    switch (role) {
    case ATK_ROLE_ALERT:
        return "AXAlert";
    case ATK_ROLE_DIALOG:
        return "AXDialog";
    case ATK_ROLE_CANVAS:
        return "AXCanvas";
    case ATK_ROLE_CHECK_BOX:
        return "AXCheckBox";
    case ATK_ROLE_COLOR_CHOOSER:
        return "AXColorWell";
    case ATK_ROLE_COLUMN_HEADER:
        return "AXColumnHeader";
    case ATK_ROLE_COMBO_BOX:
        return "AXComboBox";
    case ATK_ROLE_COMMENT:
        return "AXComment";
    case ATK_ROLE_DOCUMENT_FRAME:
        return "AXDocument";
    case ATK_ROLE_DOCUMENT_WEB:
        return "AXWebArea";
    case ATK_ROLE_EMBEDDED:
        return "AXEmbedded";
    case ATK_ROLE_ENTRY:
        return "AXTextField";
    case ATK_ROLE_FOOTER:
        return "AXFooter";
    case ATK_ROLE_FORM:
        return "AXForm";
    case ATK_ROLE_GROUPING:
        return "AXGroup";
    case ATK_ROLE_HEADING:
        return "AXHeading";
    case ATK_ROLE_IMAGE:
        return "AXImage";
    case ATK_ROLE_IMAGE_MAP:
        return "AXImageMap";
    case ATK_ROLE_LABEL:
        return "AXLabel";
    case ATK_ROLE_LINK:
        return "AXLink";
    case ATK_ROLE_LIST:
        return "AXList";
    case ATK_ROLE_LIST_BOX:
        return "AXListBox";
    case ATK_ROLE_LIST_ITEM:
        return "AXListItem";
    case ATK_ROLE_MENU:
        return "AXMenu";
    case ATK_ROLE_MENU_BAR:
        return "AXMenuBar";
    case ATK_ROLE_MENU_ITEM:
        return "AXMenuItem";
    case ATK_ROLE_PAGE_TAB:
        return "AXTab";
    case ATK_ROLE_PAGE_TAB_LIST:
        return "AXTabGroup";
    case ATK_ROLE_PANEL:
        return "AXGroup";
    case ATK_ROLE_PARAGRAPH:
        return "AXParagraph";
    case ATK_ROLE_PASSWORD_TEXT:
        return "AXPasswordField";
    case ATK_ROLE_PROGRESS_BAR:
        return "AXProgressIndicator";
    case ATK_ROLE_PUSH_BUTTON:
        return "AXButton";
    case ATK_ROLE_RADIO_BUTTON:
        return "AXRadioButton";
    case ATK_ROLE_RADIO_MENU_ITEM:
        return "AXRadioMenuItem";
    case ATK_ROLE_ROW_HEADER:
        return "AXRowHeader";
    case ATK_ROLE_CHECK_MENU_ITEM:
        return "AXCheckMenuItem";
    case ATK_ROLE_RULER:
        return "AXRuler";
    case ATK_ROLE_SCROLL_BAR:
        return "AXScrollBar";
    case ATK_ROLE_SCROLL_PANE:
        return "AXScrollArea";
    case ATK_ROLE_SECTION:
        return "AXSection";
    case ATK_ROLE_SEPARATOR:
        return "AXSeparator";
    case ATK_ROLE_SLIDER:
        return "AXSlider";
    case ATK_ROLE_SPIN_BUTTON:
        return "AXSpinButton";
    case ATK_ROLE_STATUSBAR:
        return "AXStatusBar";
    case ATK_ROLE_TABLE:
        return "AXTable";
    case ATK_ROLE_TABLE_CELL:
        return "AXCell";
    case ATK_ROLE_TABLE_COLUMN_HEADER:
        return "AXColumnHeader";
    case ATK_ROLE_TABLE_ROW:
        return "AXRow";
    case ATK_ROLE_TABLE_ROW_HEADER:
        return "AXRowHeader";
    case ATK_ROLE_TOGGLE_BUTTON:
        return "AXToggleButton";
    case ATK_ROLE_TOOL_BAR:
        return "AXToolbar";
    case ATK_ROLE_TOOL_TIP:
        return "AXUserInterfaceTooltip";
    case ATK_ROLE_TREE:
        return "AXTree";
    case ATK_ROLE_TREE_TABLE:
        return "AXTreeGrid";
    case ATK_ROLE_TREE_ITEM:
        return "AXTreeItem";
    case ATK_ROLE_WINDOW:
        return "AXWindow";
    case ATK_ROLE_UNKNOWN:
        return "AXUnknown";
#if ATK_CHECK_VERSION(2, 11, 3)
    case ATK_ROLE_ARTICLE:
        return "AXArticle";
    case ATK_ROLE_DEFINITION:
        return "AXDefinition";
    case ATK_ROLE_LOG:
        return "AXLog";
    case ATK_ROLE_MARQUEE:
        return "AXMarquee";
    case ATK_ROLE_MATH:
        return "AXMath";
    case ATK_ROLE_TIMER:
        return "AXTimer";
#endif
#if ATK_CHECK_VERSION(2, 11, 4)
    case ATK_ROLE_DESCRIPTION_LIST:
        return "AXDescriptionList";
    case ATK_ROLE_DESCRIPTION_TERM:
        return "AXDescriptionTerm";
    case ATK_ROLE_DESCRIPTION_VALUE:
        return "AXDescriptionValue";
#endif
    default:
        // We want to distinguish ATK_ROLE_UNKNOWN from a known AtkRole which
        // our DRT isn't properly handling.
        return "FIXME not identified";
    }
}

inline gchar* replaceCharactersForResults(gchar* str)
{
    String uString = String::fromUTF8(str);

    // The object replacement character is passed along to ATs so we need to be
    // able to test for their presence and do so without causing test failures.
    uString.replace(objectReplacementCharacter, "<obj>");

    // The presence of newline characters in accessible text of a single object
    // is appropriate, but it makes test results (especially the accessible tree)
    // harder to read.
    uString.replace("\n", "<\\n>");

    return g_strdup(uString.utf8().data());
}

bool checkElementState(PlatformUIElement element, AtkStateType stateType)
{
    if (!ATK_IS_OBJECT(element))
        return false;

    GRefPtr<AtkStateSet> stateSet = adoptGRef(atk_object_ref_state_set(ATK_OBJECT(element)));
    return atk_state_set_contains_state(stateSet.get(), stateType);
}

String attributesOfElement(AccessibilityUIElement* element)
{
    StringBuilder builder;

    builder.append(String::format("%s\n", element->role()->string().utf8().data()));

    // For the parent we print its role and its name, if available.
    builder.append("AXParent: ");
    AccessibilityUIElement parent = element->parentElement();
    if (AtkObject* atkParent = parent.platformUIElement()) {
        builder.append(roleToString(atkParent));
        const char* parentName = atk_object_get_name(atkParent);
        if (parentName && g_utf8_strlen(parentName, -1))
            builder.append(String::format(": %s", parentName));
    } else
        builder.append("(null)");
    builder.append("\n");

    builder.append(String::format("AXChildren: %d\n", element->childrenCount()));
    builder.append(String::format("AXPosition: { %f, %f }\n", element->x(), element->y()));
    builder.append(String::format("AXSize: { %f, %f }\n", element->width(), element->height()));

    String title = element->title()->string();
    if (!title.isEmpty())
        builder.append(String::format("%s\n", title.utf8().data()));

    String description = element->description()->string();
    if (!description.isEmpty())
        builder.append(String::format("%s\n", description.utf8().data()));

    String value = element->stringValue()->string();
    if (!value.isEmpty())
        builder.append(String::format("%s\n", value.utf8().data()));

    builder.append(String::format("AXFocusable: %d\n", element->isFocusable()));
    builder.append(String::format("AXFocused: %d\n", element->isFocused()));
    builder.append(String::format("AXSelectable: %d\n", element->isSelectable()));
    builder.append(String::format("AXSelected: %d\n", element->isSelected()));
    builder.append(String::format("AXMultiSelectable: %d\n", element->isMultiSelectable()));
    builder.append(String::format("AXEnabled: %d\n", element->isEnabled()));
    builder.append(String::format("AXExpanded: %d\n", element->isExpanded()));
    builder.append(String::format("AXRequired: %d\n", element->isRequired()));
    builder.append(String::format("AXChecked: %d\n", element->isChecked()));

    String url = element->url()->string();
    if (!url.isEmpty())
        builder.append(String::format("%s\n", url.utf8().data()));

    // We append the ATK specific attributes as a single line at the end.
    builder.append("AXPlatformAttributes: ");
    builder.append(getAtkAttributeSetAsString(element->platformUIElement(), ObjectAttributeType));

    return builder.toString();
}

static JSStringRef createStringWithAttributes(const Vector<AccessibilityUIElement>& elements)
{
    StringBuilder builder;

    for (Vector<AccessibilityUIElement>::const_iterator it = elements.begin(); it != elements.end(); ++it) {
        builder.append(attributesOfElement(const_cast<AccessibilityUIElement*>(it)));
        builder.append("\n------------\n");
    }

    return JSStringCreateWithUTF8CString(builder.toString().utf8().data());
}

static Vector<AccessibilityUIElement> getRowHeaders(AtkTable* accessible)
{
    Vector<AccessibilityUIElement> rowHeaders;

    int rowsCount = atk_table_get_n_rows(accessible);
    for (int row = 0; row < rowsCount; ++row)
        rowHeaders.append(AccessibilityUIElement(atk_table_get_row_header(accessible, row)));

    return rowHeaders;
}

static Vector<AccessibilityUIElement> getColumnHeaders(AtkTable* accessible)
{
    Vector<AccessibilityUIElement> columnHeaders;

    int columnsCount = atk_table_get_n_columns(accessible);
    for (int column = 0; column < columnsCount; ++column)
        columnHeaders.append(AccessibilityUIElement(atk_table_get_column_header(accessible, column)));

    return columnHeaders;
}

static Vector<AccessibilityUIElement> getVisibleCells(AccessibilityUIElement* element)
{
    Vector<AccessibilityUIElement> visibleCells;

    AtkTable* accessible = ATK_TABLE(element->platformUIElement());
    int rowsCount = atk_table_get_n_rows(accessible);
    int columnsCount = atk_table_get_n_columns(accessible);

    for (int row = 0; row < rowsCount; ++row) {
        for (int column = 0; column < columnsCount; ++column)
            visibleCells.append(element->cellForColumnAndRow(column, row));
    }

    return visibleCells;
}

} // namespace

JSStringRef indexRangeInTable(PlatformUIElement element, bool isRowRange)
{
    GUniquePtr<gchar> rangeString(g_strdup("{0, 0}"));

    if (!ATK_IS_OBJECT(element))
        return JSStringCreateWithUTF8CString(rangeString.get());

    AtkObject* axTable = atk_object_get_parent(ATK_OBJECT(element));
    if (!axTable || !ATK_IS_TABLE(axTable))
        return JSStringCreateWithUTF8CString(rangeString.get());

    // Look for the cell in the table.
    gint indexInParent = atk_object_get_index_in_parent(ATK_OBJECT(element));
    if (indexInParent == -1)
        return JSStringCreateWithUTF8CString(rangeString.get());

    int row = -1;
    int column = -1;
    row = atk_table_get_row_at_index(ATK_TABLE(axTable), indexInParent);
    column = atk_table_get_column_at_index(ATK_TABLE(axTable), indexInParent);

    // Get the actual values, if row and columns are valid values.
    if (row != -1 && column != -1) {
        int base = 0;
        int length = 0;
        if (isRowRange) {
            base = row;
            length = atk_table_get_row_extent_at(ATK_TABLE(axTable), row, column);
        } else {
            base = column;
            length = atk_table_get_column_extent_at(ATK_TABLE(axTable), row, column);
        }
        rangeString.reset(g_strdup_printf("{%d, %d}", base, length));
    }

    return JSStringCreateWithUTF8CString(rangeString.get());
}

void alterCurrentValue(PlatformUIElement element, int factor)
{
    if (!ATK_IS_VALUE(element))
        return;

    GValue currentValue = G_VALUE_INIT;
    atk_value_get_current_value(ATK_VALUE(element), &currentValue);

    GValue increment = G_VALUE_INIT;
    atk_value_get_minimum_increment(ATK_VALUE(element), &increment);

    GValue newValue = G_VALUE_INIT;
    g_value_init(&newValue, G_TYPE_FLOAT);

    g_value_set_float(&newValue, g_value_get_float(&currentValue) + factor * g_value_get_float(&increment));
    atk_value_set_current_value(ATK_VALUE(element), &newValue);

    g_value_unset(&newValue);
    g_value_unset(&increment);
    g_value_unset(&currentValue);
}

AccessibilityUIElement::AccessibilityUIElement(PlatformUIElement element)
    : m_element(element)
{
    if (m_element)
        g_object_ref(m_element);
}

AccessibilityUIElement::AccessibilityUIElement(const AccessibilityUIElement& other)
    : m_element(other.m_element)
{
    if (m_element)
        g_object_ref(m_element);
}

AccessibilityUIElement::~AccessibilityUIElement()
{
    if (m_element)
        g_object_unref(m_element);
}

void AccessibilityUIElement::getLinkedUIElements(Vector<AccessibilityUIElement>& elements)
{
    // FIXME: implement
}

void AccessibilityUIElement::getDocumentLinks(Vector<AccessibilityUIElement>&)
{
    // FIXME: implement
}

void AccessibilityUIElement::getChildren(Vector<AccessibilityUIElement>& children)
{
    if (!ATK_IS_OBJECT(m_element))
        return;

    int count = childrenCount();
    for (int i = 0; i < count; i++) {
        AtkObject* child = atk_object_ref_accessible_child(ATK_OBJECT(m_element), i);
        children.append(AccessibilityUIElement(child));
    }
}

void AccessibilityUIElement::getChildrenWithRange(Vector<AccessibilityUIElement>& elementVector, unsigned start, unsigned end)
{
    if (!ATK_IS_OBJECT(m_element))
        return;

    for (unsigned i = start; i < end; i++) {
        AtkObject* child = atk_object_ref_accessible_child(ATK_OBJECT(m_element), i);
        elementVector.append(AccessibilityUIElement(child));
    }
}

int AccessibilityUIElement::rowCount()
{
    if (!ATK_IS_TABLE(m_element))
        return 0;

    return atk_table_get_n_rows(ATK_TABLE(m_element));
}

int AccessibilityUIElement::columnCount()
{
    if (!ATK_IS_TABLE(m_element))
        return 0;

    return atk_table_get_n_columns(ATK_TABLE(m_element));
}

int AccessibilityUIElement::childrenCount()
{
    if (!ATK_IS_OBJECT(m_element))
        return 0;

    return atk_object_get_n_accessible_children(ATK_OBJECT(m_element));
}

AccessibilityUIElement AccessibilityUIElement::elementAtPoint(int x, int y)
{
    if (!ATK_IS_COMPONENT(m_element))
        return nullptr;

    GRefPtr<AtkObject> objectAtPoint = adoptGRef(atk_component_ref_accessible_at_point(ATK_COMPONENT(m_element), x, y, ATK_XY_WINDOW));
    return AccessibilityUIElement(objectAtPoint ? objectAtPoint.get() : m_element);
}

AccessibilityUIElement AccessibilityUIElement::linkedUIElementAtIndex(unsigned index)
{
    // FIXME: implement
    return nullptr;
}

AccessibilityUIElement AccessibilityUIElement::getChildAtIndex(unsigned index)
{
    if (!ATK_IS_OBJECT(m_element))
        return nullptr;

    Vector<AccessibilityUIElement> children;
    getChildrenWithRange(children, index, index + 1);

    if (children.size() == 1)
        return children.at(0);

    return nullptr;
}

unsigned AccessibilityUIElement::indexOfChild(AccessibilityUIElement* element)
{ 
    // FIXME: implement
    return 0;
}

JSStringRef AccessibilityUIElement::allAttributes()
{
    if (!ATK_IS_OBJECT(m_element))
        return JSStringCreateWithCharacters(0, 0);

    return JSStringCreateWithUTF8CString(attributesOfElement(this).utf8().data());
}

JSStringRef AccessibilityUIElement::attributesOfLinkedUIElements()
{
    // FIXME: implement
    return JSStringCreateWithCharacters(0, 0);
}

JSStringRef AccessibilityUIElement::attributesOfDocumentLinks()
{
    // FIXME: implement
    return JSStringCreateWithCharacters(0, 0);
}

AccessibilityUIElement AccessibilityUIElement::titleUIElement()
{
    if (!ATK_IS_OBJECT(m_element))
        return nullptr;

    AtkRelationSet* set = atk_object_ref_relation_set(ATK_OBJECT(m_element));
    if (!set)
        return nullptr;

    AtkObject* target = nullptr;
    int count = atk_relation_set_get_n_relations(set);
    for (int i = 0; i < count; i++) {
        AtkRelation* relation = atk_relation_set_get_relation(set, i);
        if (atk_relation_get_relation_type(relation) == ATK_RELATION_LABELLED_BY) {
            GPtrArray* targetList = atk_relation_get_target(relation);
            if (targetList->len)
                target = static_cast<AtkObject*>(g_ptr_array_index(targetList, 0));
        }
    }

    g_object_unref(set);
    return target ? AccessibilityUIElement(target) : nullptr;
}

AccessibilityUIElement AccessibilityUIElement::parentElement()
{
    if (!ATK_IS_OBJECT(m_element))
        return nullptr;

    AtkObject* parent =  atk_object_get_parent(ATK_OBJECT(m_element));
    return parent ? AccessibilityUIElement(parent) : nullptr;
}

JSStringRef AccessibilityUIElement::attributesOfChildren()
{
    if (!ATK_IS_OBJECT(m_element))
        return JSStringCreateWithCharacters(0, 0);

    Vector<AccessibilityUIElement> children;
    getChildren(children);

    return createStringWithAttributes(children);
}

JSStringRef AccessibilityUIElement::parameterizedAttributeNames()
{
    // FIXME: implement
    return JSStringCreateWithCharacters(0, 0);
}

JSStringRef AccessibilityUIElement::role()
{
    if (!ATK_IS_OBJECT(m_element))
        return JSStringCreateWithCharacters(0, 0);

    if (!atk_object_get_role(ATK_OBJECT(m_element)))
        return JSStringCreateWithCharacters(0, 0);

    GUniquePtr<char> roleStringWithPrefix(g_strdup_printf("AXRole: %s", roleToString(ATK_OBJECT(m_element))));
    return JSStringCreateWithUTF8CString(roleStringWithPrefix.get());
}

JSStringRef AccessibilityUIElement::subrole()
{
    return nullptr;
}

JSStringRef AccessibilityUIElement::roleDescription()
{
    return nullptr;
}

JSStringRef AccessibilityUIElement::computedRoleString()
{
    // FIXME: implement http://webkit.org/b/128420
    return nullptr;
}

JSStringRef AccessibilityUIElement::title()
{
    if (!ATK_IS_OBJECT(m_element))
        return JSStringCreateWithCharacters(0, 0);

    const gchar* name = atk_object_get_name(ATK_OBJECT(m_element));
    GUniquePtr<gchar> axTitle(g_strdup_printf("AXTitle: %s", name ? name : ""));

    return JSStringCreateWithUTF8CString(axTitle.get());
}

JSStringRef AccessibilityUIElement::description()
{
    if (!ATK_IS_OBJECT(m_element))
        return JSStringCreateWithCharacters(0, 0);

    const gchar* description = atk_object_get_description(ATK_OBJECT(m_element));
    if (!description)
        return JSStringCreateWithCharacters(0, 0);

    GUniquePtr<gchar> axDesc(g_strdup_printf("AXDescription: %s", description));

    return JSStringCreateWithUTF8CString(axDesc.get());
}

JSStringRef AccessibilityUIElement::stringValue()
{
    if (!ATK_IS_TEXT(m_element))
        return JSStringCreateWithCharacters(0, 0);

    GUniquePtr<gchar> text(atk_text_get_text(ATK_TEXT(m_element), 0, -1));
    GUniquePtr<gchar> textWithReplacedCharacters(replaceCharactersForResults(text.get()));
    GUniquePtr<gchar> axValue(g_strdup_printf("AXValue: %s", textWithReplacedCharacters.get()));

    return JSStringCreateWithUTF8CString(axValue.get());
}

JSStringRef AccessibilityUIElement::language()
{
    if (!ATK_IS_OBJECT(m_element))
        return JSStringCreateWithCharacters(0, 0);

    const gchar* locale = atk_object_get_object_locale(ATK_OBJECT(m_element));
    if (!locale)
        return JSStringCreateWithCharacters(0, 0);

    GUniquePtr<char> axValue(g_strdup_printf("AXLanguage: %s", locale));
    return JSStringCreateWithUTF8CString(axValue.get());
}

JSStringRef AccessibilityUIElement::helpText() const
{
    if (!ATK_IS_OBJECT(m_element))
        return JSStringCreateWithCharacters(0, 0);

    AtkRelationSet* relationSet = atk_object_ref_relation_set(ATK_OBJECT(m_element));
    if (!relationSet)
        return nullptr;

    AtkRelation* relation = atk_relation_set_get_relation_by_type(relationSet, ATK_RELATION_DESCRIBED_BY);
    if (!relation)
        return nullptr;

    GPtrArray* targetList = atk_relation_get_target(relation);
    if (!targetList || !targetList->len)
        return nullptr;

    StringBuilder builder;
    builder.append("AXHelp: ");

    for (int targetCount = 0; targetCount < targetList->len; targetCount++) {
        if (AtkObject* target = static_cast<AtkObject*>(g_ptr_array_index(targetList, targetCount))) {
            GUniquePtr<gchar> text(atk_text_get_text(ATK_TEXT(target), 0, -1));
            if (!builder.isEmpty())
                builder.append(" ");
            builder.append(text.get());
        }
    }

    g_object_unref(relationSet);

    return JSStringCreateWithUTF8CString(builder.toString().utf8().data());

}

double AccessibilityUIElement::x()
{
    if (!ATK_IS_COMPONENT(m_element))
        return 0;

    int x, y;
    atk_component_get_position(ATK_COMPONENT(m_element), &x, &y, ATK_XY_SCREEN);

    return x;
}

double AccessibilityUIElement::y()
{
    if (!ATK_IS_COMPONENT(m_element))
        return 0;

    int x, y;
    atk_component_get_position(ATK_COMPONENT(m_element), &x, &y, ATK_XY_SCREEN);

    return y;
}

double AccessibilityUIElement::width()
{
    if (!ATK_IS_COMPONENT(m_element))
        return 0;

    int width, height;
    atk_component_get_size(ATK_COMPONENT(m_element), &width, &height);

    return width;
}

double AccessibilityUIElement::height()
{
    if (!ATK_IS_COMPONENT(m_element))
        return 0;

    int width, height;
    atk_component_get_size(ATK_COMPONENT(m_element), &width, &height);

    return height;
}

double AccessibilityUIElement::clickPointX()
{
    if (!ATK_IS_COMPONENT(m_element))
        return 0;

    int x, y;
    atk_component_get_position(ATK_COMPONENT(m_element), &x, &y, ATK_XY_WINDOW);

    int width, height;
    atk_component_get_size(ATK_COMPONENT(m_element), &width, &height);

    return x + width / 2.0;
}

double AccessibilityUIElement::clickPointY()
{
    if (!ATK_IS_COMPONENT(m_element))
        return 0;

    int x, y;
    atk_component_get_position(ATK_COMPONENT(m_element), &x, &y, ATK_XY_WINDOW);

    int width, height;
    atk_component_get_size(ATK_COMPONENT(m_element), &width, &height);

    return y + height / 2.0;
}

JSStringRef AccessibilityUIElement::orientation() const
{
    if (!ATK_IS_OBJECT(m_element))
        return JSStringCreateWithCharacters(0, 0);

    const char* axOrientation = nullptr;
    if (checkElementState(m_element, ATK_STATE_HORIZONTAL))
        axOrientation = "AXOrientation: AXHorizontalOrientation";
    else if (checkElementState(m_element, ATK_STATE_VERTICAL))
        axOrientation = "AXOrientation: AXVerticalOrientation";

    if (!axOrientation)
        return JSStringCreateWithCharacters(0, 0);

    return JSStringCreateWithUTF8CString(axOrientation);
}

double AccessibilityUIElement::intValue() const
{
    if (!ATK_IS_OBJECT(m_element))
        return 0;

    if (ATK_IS_VALUE(m_element)) {
        GValue value = G_VALUE_INIT;
        atk_value_get_current_value(ATK_VALUE(m_element), &value);
        if (!G_VALUE_HOLDS_FLOAT(&value))
            return 0;
        return g_value_get_float(&value);
    }

    // Consider headings as an special case when returning the "int value" of
    // an AccessibilityUIElement, so we can reuse some tests to check the level
    // both for HTML headings and objects with the aria-level attribute.
    if (atk_object_get_role(ATK_OBJECT(m_element)) == ATK_ROLE_HEADING) {
        String headingLevel = getAttributeSetValueForId(ATK_OBJECT(m_element), ObjectAttributeType, "level");
        bool ok;
        double headingLevelValue = headingLevel.toDouble(&ok);
        if (ok)
            return headingLevelValue;
    }

    return 0;
}

double AccessibilityUIElement::minValue()
{
    if (!ATK_IS_VALUE(m_element))
        return 0;

    GValue value = G_VALUE_INIT;
    atk_value_get_minimum_value(ATK_VALUE(m_element), &value);
    if (!G_VALUE_HOLDS_FLOAT(&value))
        return 0;
    return g_value_get_float(&value);
}

double AccessibilityUIElement::maxValue()
{
    if (!ATK_IS_VALUE(m_element))
        return 0;

    GValue value = G_VALUE_INIT;
    atk_value_get_maximum_value(ATK_VALUE(m_element), &value);
    if (!G_VALUE_HOLDS_FLOAT(&value))
        return 0;
    return g_value_get_float(&value);
}

JSStringRef AccessibilityUIElement::valueDescription()
{
    // FIXME: implement after it has been implemented in ATK.
    // See: https://bugzilla.gnome.org/show_bug.cgi?id=684576
    return JSStringCreateWithCharacters(0, 0);
}

bool AccessibilityUIElement::isEnabled()
{
    return checkElementState(m_element, ATK_STATE_ENABLED);
}

int AccessibilityUIElement::insertionPointLineNumber()
{
    // FIXME: implement
    return 0;
}

bool AccessibilityUIElement::isPressActionSupported()
{
    if (!ATK_IS_ACTION(m_element))
        return false;

    const gchar* actionName = atk_action_get_name(ATK_ACTION(m_element), 0);
    return equalIgnoringCase(actionName, String("press")) || equalIgnoringCase(actionName, String("jump"));
}

bool AccessibilityUIElement::isIncrementActionSupported()
{
    // FIXME: implement
    return false;
}

bool AccessibilityUIElement::isDecrementActionSupported()
{
    // FIXME: implement
    return false;
}

bool AccessibilityUIElement::isRequired() const
{
    return checkElementState(m_element, ATK_STATE_REQUIRED);
}

bool AccessibilityUIElement::isFocused() const
{
    if (!ATK_IS_OBJECT(m_element))
        return false;

    GRefPtr<AtkStateSet> stateSet = adoptGRef(atk_object_ref_state_set(ATK_OBJECT(m_element)));
    gboolean isFocused = atk_state_set_contains_state(stateSet.get(), ATK_STATE_FOCUSED);

    return isFocused;
}

bool AccessibilityUIElement::isSelected() const
{
    return checkElementState(m_element, ATK_STATE_SELECTED);
}

int AccessibilityUIElement::hierarchicalLevel() const
{
    // FIXME: implement
    return 0;
}

bool AccessibilityUIElement::ariaIsGrabbed() const
{
    return false;
}

JSStringRef AccessibilityUIElement::ariaDropEffects() const
{   
    return nullptr; 
}

bool AccessibilityUIElement::isExpanded() const
{
    if (!ATK_IS_OBJECT(m_element))
        return false;

    GRefPtr<AtkStateSet> stateSet = adoptGRef(atk_object_ref_state_set(ATK_OBJECT(m_element)));
    gboolean isExpanded = atk_state_set_contains_state(stateSet.get(), ATK_STATE_EXPANDED);

    return isExpanded;
}

bool AccessibilityUIElement::isChecked() const
{
    if (!ATK_IS_OBJECT(m_element))
        return false;

    GRefPtr<AtkStateSet> stateSet = adoptGRef(atk_object_ref_state_set(ATK_OBJECT(m_element)));
    gboolean isChecked = atk_state_set_contains_state(stateSet.get(), ATK_STATE_CHECKED);

    return isChecked;
}

bool AccessibilityUIElement::isIndeterminate() const
{
    if (!ATK_IS_OBJECT(m_element))
        return false;

    GRefPtr<AtkStateSet> stateSet = adoptGRef(atk_object_ref_state_set(ATK_OBJECT(m_element)));
    return atk_state_set_contains_state(stateSet.get(), ATK_STATE_INDETERMINATE);
}

JSStringRef AccessibilityUIElement::attributesOfColumnHeaders()
{
    if (!ATK_IS_TABLE(m_element))
        return JSStringCreateWithCharacters(0, 0);

    Vector<AccessibilityUIElement> columnHeaders = getColumnHeaders(ATK_TABLE(m_element));
    return createStringWithAttributes(columnHeaders);
}

JSStringRef AccessibilityUIElement::attributesOfRowHeaders()
{
    if (!ATK_IS_TABLE(m_element))
        return JSStringCreateWithCharacters(0, 0);

    Vector<AccessibilityUIElement> rowHeaders = getRowHeaders(ATK_TABLE(m_element));
    return createStringWithAttributes(rowHeaders);
}

JSStringRef AccessibilityUIElement::attributesOfColumns()
{
    // FIXME: implement
    return JSStringCreateWithCharacters(0, 0);
}

JSStringRef AccessibilityUIElement::attributesOfRows()
{
    // FIXME: implement
    return JSStringCreateWithCharacters(0, 0);
}

JSStringRef AccessibilityUIElement::attributesOfVisibleCells()
{
    if (!ATK_IS_TABLE(m_element))
        return JSStringCreateWithCharacters(0, 0);

    Vector<AccessibilityUIElement> visibleCells = getVisibleCells(this);
    return createStringWithAttributes(visibleCells);
}

JSStringRef AccessibilityUIElement::attributesOfHeader()
{
    // FIXME: implement
    return JSStringCreateWithCharacters(0, 0);
}

int AccessibilityUIElement::indexInTable()
{
    // FIXME: implement
    return 0;
}

JSStringRef AccessibilityUIElement::rowIndexRange()
{
    // Range in table for rows.
    return indexRangeInTable(m_element, true);
}

JSStringRef AccessibilityUIElement::columnIndexRange()
{
    // Range in table for columns.
    return indexRangeInTable(m_element, false);
}

int AccessibilityUIElement::lineForIndex(int index)
{
    if (!ATK_IS_TEXT(m_element))
        return -1;

    if (index < 0 || index > atk_text_get_character_count(ATK_TEXT(m_element)))
        return -1;

    GUniquePtr<gchar> text(atk_text_get_text(ATK_TEXT(m_element), 0, index));
    int lineNo = 0;
    for (gchar* offset = text.get(); *offset; ++offset) {
        if (*offset == '\n')
            ++lineNo;
    }

    return lineNo;
}

JSStringRef AccessibilityUIElement::boundsForRange(unsigned location, unsigned length)
{
    // FIXME: implement
    return JSStringCreateWithCharacters(0, 0);
}

JSStringRef AccessibilityUIElement::stringForRange(unsigned location, unsigned length)
{
    if (!ATK_IS_TEXT(m_element))
        return JSStringCreateWithCharacters(0, 0);

    String string = atk_text_get_text(ATK_TEXT(m_element), location, location + length);
    return JSStringCreateWithUTF8CString(string.utf8().data());
} 

JSStringRef AccessibilityUIElement::attributedStringForRange(unsigned, unsigned)
{
    // FIXME: implement
    return JSStringCreateWithCharacters(0, 0);
}

bool AccessibilityUIElement::attributedStringRangeIsMisspelled(unsigned location, unsigned length)
{
    // FIXME: implement
    return false;
}

unsigned AccessibilityUIElement::uiElementCountForSearchPredicate(JSContextRef context, AccessibilityUIElement* startElement, bool isDirectionNext, JSValueRef searchKey, JSStringRef searchText, bool visibleOnly, bool immediateDescendantsOnly)
{
    // FIXME: implement
    return 0;
}

AccessibilityUIElement AccessibilityUIElement::uiElementForSearchPredicate(JSContextRef context, AccessibilityUIElement* startElement, bool isDirectionNext, JSValueRef searchKey, JSStringRef searchText, bool visibleOnly, bool immediateDescendantsOnly)
{
    // FIXME: implement
    return nullptr;
}

JSStringRef AccessibilityUIElement::selectTextWithCriteria(JSContextRef context, JSStringRef ambiguityResolution, JSValueRef searchStrings, JSStringRef replacementString)
{
    // FIXME: implement
    return nullptr;
}

AccessibilityUIElement AccessibilityUIElement::cellForColumnAndRow(unsigned column, unsigned row)
{
    if (!ATK_IS_TABLE(m_element))
        return nullptr;

    // Adopt the AtkObject representing the cell because
    // at_table_ref_at() transfers full ownership.
    GRefPtr<AtkObject> foundCell = adoptGRef(atk_table_ref_at(ATK_TABLE(m_element), row, column));
    return foundCell ? AccessibilityUIElement(foundCell.get()) : nullptr;
}

JSStringRef AccessibilityUIElement::selectedTextRange()
{
    if (!ATK_IS_TEXT(m_element))
        return JSStringCreateWithCharacters(0, 0);

    gint start, end;
    g_free(atk_text_get_selection(ATK_TEXT(m_element), 0, &start, &end));

    GUniquePtr<gchar> selection(g_strdup_printf("{%d, %d}", start, end - start));
    return JSStringCreateWithUTF8CString(selection.get());
}

void AccessibilityUIElement::setSelectedTextRange(unsigned location, unsigned length)
{
    if (!ATK_IS_TEXT(m_element))
        return;

    atk_text_set_selection(ATK_TEXT(m_element), 0, location, location + length);
}

JSStringRef AccessibilityUIElement::stringAttributeValue(JSStringRef attribute)
{
    if (!ATK_IS_OBJECT(m_element))
        return JSStringCreateWithCharacters(0, 0);

    String atkAttributeName = coreAttributeToAtkAttribute(attribute);

    // Try object attributes first.
    String attributeValue = getAttributeSetValueForId(ATK_OBJECT(m_element), ObjectAttributeType, atkAttributeName);

    // Try text attributes if the requested one was not found and we have an AtkText object.
    if (attributeValue.isEmpty() && ATK_IS_TEXT(m_element))
        attributeValue = getAttributeSetValueForId(ATK_OBJECT(m_element), TextAttributeType, atkAttributeName);

    // Additional check to make sure that the exposure of the state ATK_STATE_INVALID_ENTRY
    // is consistent with the exposure of aria-invalid as a text attribute, if present.
    if (atkAttributeName == attributesMap[InvalidNameIndex][AtkDomain]) {
        bool isInvalidState = checkElementState(m_element, ATK_STATE_INVALID_ENTRY);
        if (attributeValue.isEmpty())
            return JSStringCreateWithUTF8CString(isInvalidState ? "true" : "false");

        // If the text attribute was there, check that it's consistent with
        // what the state says or force the test to fail otherwise.
        bool isAriaInvalid = attributeValue != "false";
        if (isInvalidState != isAriaInvalid)
            return JSStringCreateWithCharacters(0, 0);
    }

    return JSStringCreateWithUTF8CString(attributeValue.utf8().data());
}

double AccessibilityUIElement::numberAttributeValue(JSStringRef attribute)
{
    // FIXME: implement
    return 0;
}

bool AccessibilityUIElement::boolAttributeValue(JSStringRef attribute)
{
    // FIXME: implement
    return false;
}

bool AccessibilityUIElement::isAttributeSettable(JSStringRef attribute)
{
    if (!ATK_IS_OBJECT(m_element))
        return false;

    String attributeString = jsStringToWTFString(attribute);
    if (attributeString == "AXValue")
        return checkElementState(m_element, ATK_STATE_EDITABLE);

    return false;
}

bool AccessibilityUIElement::isAttributeSupported(JSStringRef attribute)
{
    if (!ATK_IS_OBJECT(m_element))
        return false;

    String atkAttributeName = coreAttributeToAtkAttribute(attribute);
    if (atkAttributeName.isEmpty())
        return false;

    // For now, an attribute is supported whether it's exposed as a object or a text attribute.
    String attributeValue = getAttributeSetValueForId(ATK_OBJECT(m_element), ObjectAttributeType, atkAttributeName);
    if (attributeValue.isEmpty())
        attributeValue = getAttributeSetValueForId(ATK_OBJECT(m_element), TextAttributeType, atkAttributeName);

    return !attributeValue.isEmpty();
}

void AccessibilityUIElement::increment()
{
    alterCurrentValue(m_element, 1);
}

void AccessibilityUIElement::decrement()
{
    alterCurrentValue(m_element, -1);
}

void AccessibilityUIElement::press()
{
    if (!ATK_IS_ACTION(m_element))
        return;

    // Only one action per object is supported so far.
    atk_action_do_action(ATK_ACTION(m_element), 0);
}

void AccessibilityUIElement::showMenu()
{
    // FIXME: implement
}

AccessibilityUIElement accessibilityElementAtIndex(AtkObject* element, AtkRelationType relationType, unsigned index)
{
    if (!ATK_IS_OBJECT(element))
        return nullptr;

    AtkRelationSet* relationSet = atk_object_ref_relation_set(element);
    if (!relationSet)
        return nullptr;

    AtkRelation* relation = atk_relation_set_get_relation_by_type(relationSet, relationType);
    if (!relation)
        return nullptr;

    GPtrArray* targetList = atk_relation_get_target(relation);
    if (!targetList || !targetList->len || index >= targetList->len)
        return nullptr;

    AtkObject* target = static_cast<AtkObject*>(g_ptr_array_index(targetList, index));
    g_object_unref(relationSet);

    return target ? AccessibilityUIElement(target) : nullptr;
}

AccessibilityUIElement AccessibilityUIElement::disclosedRowAtIndex(unsigned index)
{
    return nullptr;
}

AccessibilityUIElement AccessibilityUIElement::ariaOwnsElementAtIndex(unsigned index)
{
    return nullptr;
}

AccessibilityUIElement AccessibilityUIElement::ariaFlowToElementAtIndex(unsigned index)
{
    return accessibilityElementAtIndex(m_element, ATK_RELATION_FLOWS_TO, index);
}

AccessibilityUIElement AccessibilityUIElement::ariaControlsElementAtIndex(unsigned index)
{
    return accessibilityElementAtIndex(m_element, ATK_RELATION_CONTROLLER_FOR, index);
}

AccessibilityUIElement AccessibilityUIElement::selectedRowAtIndex(unsigned index)
{
    return nullptr;
}

AccessibilityUIElement AccessibilityUIElement::rowAtIndex(unsigned index)
{
    return nullptr;
}

AccessibilityUIElement AccessibilityUIElement::disclosedByRow()
{
    return nullptr;
}

JSStringRef AccessibilityUIElement::accessibilityValue() const
{
    // FIXME: implement
    return JSStringCreateWithCharacters(0, 0);
}

JSStringRef AccessibilityUIElement::documentEncoding()
{
    if (!ATK_IS_DOCUMENT(m_element))
        return JSStringCreateWithCharacters(0, 0);

    AtkRole role = atk_object_get_role(ATK_OBJECT(m_element));
    if (role != ATK_ROLE_DOCUMENT_FRAME)
        return JSStringCreateWithCharacters(0, 0);

    return JSStringCreateWithUTF8CString(atk_document_get_attribute_value(ATK_DOCUMENT(m_element), "Encoding"));
}

JSStringRef AccessibilityUIElement::documentURI()
{
    if (!ATK_IS_DOCUMENT(m_element))
        return JSStringCreateWithCharacters(0, 0);

    AtkRole role = atk_object_get_role(ATK_OBJECT(m_element));
    if (role != ATK_ROLE_DOCUMENT_FRAME)
        return JSStringCreateWithCharacters(0, 0);

    return JSStringCreateWithUTF8CString(atk_document_get_attribute_value(ATK_DOCUMENT(m_element), "URI"));
}

JSStringRef AccessibilityUIElement::url()
{
    if (!ATK_IS_HYPERLINK_IMPL(m_element))
        return JSStringCreateWithCharacters(0, 0);

    AtkHyperlink* hyperlink = atk_hyperlink_impl_get_hyperlink(ATK_HYPERLINK_IMPL(m_element));
    GUniquePtr<char> hyperlinkURI(atk_hyperlink_get_uri(hyperlink, 0));

    // Build the result string, stripping the absolute URL paths if present.
    char* localURI = g_strstr_len(hyperlinkURI.get(), -1, "LayoutTests");
    String axURL = String::format("AXURL: %s", localURI ? localURI : hyperlinkURI.get());
    return JSStringCreateWithUTF8CString(axURL.utf8().data());
}

bool AccessibilityUIElement::addNotificationListener(JSObjectRef functionCallback)
{
    if (!functionCallback)
        return false;

    // Only one notification listener per element.
    if (m_notificationHandler)
        return false;

    m_notificationHandler = AccessibilityNotificationHandler::create();
    m_notificationHandler->setPlatformElement(platformUIElement());
    m_notificationHandler->setNotificationFunctionCallback(functionCallback);

    return true;
}

void AccessibilityUIElement::removeNotificationListener()
{
    // Programmers should not be trying to remove a listener that's already removed.
    ASSERT(m_notificationHandler);

    m_notificationHandler = nullptr;
}

bool AccessibilityUIElement::isFocusable() const
{
    if (!ATK_IS_OBJECT(m_element))
        return false;

    GRefPtr<AtkStateSet> stateSet = adoptGRef(atk_object_ref_state_set(ATK_OBJECT(m_element)));
    gboolean isFocusable = atk_state_set_contains_state(stateSet.get(), ATK_STATE_FOCUSABLE);

    return isFocusable;
}

bool AccessibilityUIElement::isSelectable() const
{
    return checkElementState(m_element, ATK_STATE_SELECTABLE);
}

bool AccessibilityUIElement::isMultiSelectable() const
{
    return checkElementState(m_element, ATK_STATE_MULTISELECTABLE);
}

bool AccessibilityUIElement::isSelectedOptionActive() const
{
    return checkElementState(m_element, ATK_STATE_ACTIVE);
}

bool AccessibilityUIElement::isVisible() const
{
    // FIXME: implement
    return false;
}

bool AccessibilityUIElement::isOffScreen() const
{
    // FIXME: implement
    return false;
}

bool AccessibilityUIElement::isCollapsed() const
{
    // FIXME: implement
    return false;
}

bool AccessibilityUIElement::isIgnored() const
{
    // FIXME: implement
    return false;
}

bool AccessibilityUIElement::hasPopup() const
{
    if (!ATK_IS_OBJECT(m_element))
        return false;

    String hasPopupValue = getAttributeSetValueForId(ATK_OBJECT(m_element), ObjectAttributeType, "haspopup");
    return equalIgnoringCase(hasPopupValue, "true");
}

void AccessibilityUIElement::takeFocus()
{
    // FIXME: implement
}

void AccessibilityUIElement::takeSelection()
{
    // FIXME: implement
}

void AccessibilityUIElement::addSelection()
{
    // FIXME: implement
}

void AccessibilityUIElement::removeSelection()
{
    // FIXME: implement
}

void AccessibilityUIElement::scrollToMakeVisible()
{
    // FIXME: implement
}

void AccessibilityUIElement::scrollToMakeVisibleWithSubFocus(int x, int y, int width, int height)
{
    // FIXME: implement
}

void AccessibilityUIElement::scrollToGlobalPoint(int x, int y)
{
    // FIXME: implement
}

JSStringRef AccessibilityUIElement::classList() const
{
    // FIXME: implement
    return nullptr;
}

JSStringRef stringAtOffset(PlatformUIElement element, AtkTextBoundary boundary, int offset)
{
    if (!ATK_IS_TEXT(element))
        return JSStringCreateWithCharacters(0, 0);

    gint startOffset, endOffset;
    StringBuilder builder;

#if ATK_CHECK_VERSION(2, 10, 0)
    AtkTextGranularity granularity;
    switch (boundary) {
    case ATK_TEXT_BOUNDARY_CHAR:
        granularity = ATK_TEXT_GRANULARITY_CHAR;
        break;
    case ATK_TEXT_BOUNDARY_WORD_START:
        granularity = ATK_TEXT_GRANULARITY_WORD;
        break;
    case ATK_TEXT_BOUNDARY_LINE_START:
        granularity = ATK_TEXT_GRANULARITY_LINE;
        break;
    case ATK_TEXT_BOUNDARY_SENTENCE_START:
        granularity = ATK_TEXT_GRANULARITY_SENTENCE;
        break;
    default:
        return JSStringCreateWithCharacters(0, 0);
    }

    builder.append(atk_text_get_string_at_offset(ATK_TEXT(element), offset, granularity, &startOffset, &endOffset));
#else
    builder.append(atk_text_get_text_at_offset(ATK_TEXT(element), offset, boundary, &startOffset, &endOffset));
#endif
    builder.append(String::format(", %i, %i", startOffset, endOffset));
    return JSStringCreateWithUTF8CString(builder.toString().utf8().data());
}

JSStringRef AccessibilityUIElement::characterAtOffset(int offset)
{
    return stringAtOffset(m_element, ATK_TEXT_BOUNDARY_CHAR, offset);
}

JSStringRef AccessibilityUIElement::wordAtOffset(int offset)
{
    return stringAtOffset(m_element, ATK_TEXT_BOUNDARY_WORD_START, offset);
}

JSStringRef AccessibilityUIElement::lineAtOffset(int offset)
{
    return stringAtOffset(m_element, ATK_TEXT_BOUNDARY_LINE_START, offset);
}

JSStringRef AccessibilityUIElement::sentenceAtOffset(int offset)
{
    return stringAtOffset(m_element, ATK_TEXT_BOUNDARY_SENTENCE_START, offset);
}

unsigned AccessibilityUIElement::selectedChildrenCount() const
{
    if (!ATK_IS_SELECTION(m_element))
        return 0;

    return atk_selection_get_selection_count(ATK_SELECTION(m_element));
}

AccessibilityUIElement AccessibilityUIElement::selectedChildAtIndex(unsigned index) const
{
    if (!ATK_IS_SELECTION(m_element))
        return nullptr;

    GRefPtr<AtkObject> child = adoptGRef(atk_selection_ref_selection(ATK_SELECTION(m_element), index));
    return child ? AccessibilityUIElement(child.get()) : nullptr;
}

void AccessibilityUIElement::setSelectedChildAtIndex(unsigned index) const
{
    if (!ATK_IS_SELECTION(m_element))
        return;

    atk_selection_add_selection(ATK_SELECTION(m_element), index);
}

void AccessibilityUIElement::removeSelectionAtIndex(unsigned index) const
{
    if (!ATK_IS_SELECTION(m_element))
        return;

    atk_selection_remove_selection(ATK_SELECTION(m_element), index);
}

#endif
