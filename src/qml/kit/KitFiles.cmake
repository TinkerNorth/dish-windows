# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Dish contributors.
#
# The canonical list of design-kit QML files, included by BOTH modules that
# ship the kit:
#   - the app's `Dish` qt_add_qml_module (URI Dish.Chrome), and
#   - the installer's `dish_setup_kit` qt_add_qml_module (same URI, different
#     OUTPUT_DIRECTORY, linked only into dish-setup-ui.exe).
#
# Two lists, one source of truth (spec D3):
#   DISH_KIT_QML_FILES         everything, for the app module
#   DISH_KIT_QML_FILES_SHARED  the same minus KitGallery.qml, which imports the
#                              app's page stack and has no business in the
#                              installer binary
#
# Paths are relative to the REPO ROOT because the app module is declared there
# and Qt derives each file's resource alias from the path relative to the
# declaring CMakeLists' directory. installer/CMakeLists.txt makes them absolute
# and re-states the same alias, so both modules lay the kit out identically.
#
# Adding a kit component means adding it here once: forgetting the installer
# copy is the failure mode this file exists to make impossible.

set(DISH_KIT_QML_FILES
    src/qml/kit/SectionHeader.qml
    src/qml/kit/KitButton.qml
    src/qml/kit/OutlineButton.qml
    src/qml/kit/Card.qml
    src/qml/kit/StatusDot.qml
    src/qml/kit/BrandGlyph.qml
    src/qml/kit/LabeledSwitch.qml
    src/qml/kit/KitTextField.qml
    src/qml/kit/Page.qml
    src/qml/kit/ContentDialog.qml
    src/qml/kit/Eyebrow.qml
    src/qml/kit/SegmentedControl.qml
    src/qml/kit/ComboButton.qml
    src/qml/kit/SliderRow.qml
    src/qml/kit/RadioMark.qml
    src/qml/kit/RowButton.qml
    src/qml/kit/CapabilityChip.qml
    src/qml/kit/LiveStat.qml
    src/qml/kit/DishProgressBar.qml
    # The one button type; KitButton/OutlineButton are aliases over it.
    src/qml/kit/DishButton.qml
    # The one themed tooltip. A bare ToolTip resolves Basic's system-palette
    # delegate, which reads as an unthemed white slab in both appearances.
    src/qml/kit/DishToolTip.qml
    src/qml/kit/Callout.qml
    src/qml/kit/SelectRow.qml
    src/qml/kit/OptionCard.qml
    src/qml/kit/ActionCard.qml
    # The shared loading/error/empty states every page binds. See
    # ARCHITECTURE.md §7 "bind all the states".
    src/qml/kit/LoadingSpinner.qml
    src/qml/kit/ErrorBanner.qml
    src/qml/kit/EmptyState.qml
    src/qml/kit/NotificationToastHost.qml
    src/qml/kit/WireLine.qml
    # WireLine's vertical sibling and the hero rail's dashed ellipse. Kit
    # members rather than installer-local files because the wire vocabulary is
    # one system and the app may draw them next.
    src/qml/kit/BeamLine.qml
    src/qml/kit/OrbitArc.qml
    src/qml/kit/StepList.qml
    src/qml/kit/BindingStrip.qml
    src/qml/kit/CapabilityTable.qml
    src/qml/kit/WizardBanner.qml
    src/qml/kit/ConfirmDialog.qml
    src/qml/kit/ApplyOverlay.qml
    src/qml/kit/BlockerDialog.qml
    # The visual-regression gallery. Deliberately has no runtime entry
    # point: it is reached only by editing Main.qml.
    src/qml/kit/KitGallery.qml)

# The installer ships every kit component except the gallery, which binds the
# app's own pages and would drag them into dish-setup-ui.exe.
set(DISH_KIT_QML_FILES_SHARED ${DISH_KIT_QML_FILES})
list(REMOVE_ITEM DISH_KIT_QML_FILES_SHARED src/qml/kit/KitGallery.qml)
