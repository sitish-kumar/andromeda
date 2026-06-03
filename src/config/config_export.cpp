#include "config/config_export.h"

#include "config/schema/config_schema.h"
#include "config/schema/engine.h"
#include "core/key_chord.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace config_export {
  namespace schema = noctalia::config::schema;

  namespace {

    toml::array stringArray(const std::vector<std::string>& values) {
      toml::array array;
      for (const auto& value : values) {
        array.push_back(value);
      }
      return array;
    }

    void insertWidgetSettingValue(toml::table& table, std::string_view key, const WidgetSettingValue& value) {
      std::visit(
          [&](const auto& concrete) {
            using T = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<T, std::vector<std::string>>) {
              table.insert_or_assign(key, stringArray(concrete));
            } else {
              table.insert_or_assign(key, concrete);
            }
          },
          value
      );
    }

    toml::table widgetConfigTable(const WidgetConfig& widget) {
      toml::table table;
      if (!widget.type.empty()) {
        table.insert_or_assign("type", widget.type);
      }

      std::vector<std::string> keys;
      keys.reserve(widget.settings.size());
      for (const auto& [key, value] : widget.settings) {
        (void)value;
        keys.push_back(key);
      }
      std::sort(keys.begin(), keys.end());

      for (const auto& key : keys) {
        insertWidgetSettingValue(table, key, widget.settings.at(key));
      }
      return table;
    }

    toml::array capsuleGroupArray(const std::vector<BarCapsuleGroupStyle>& groups) {
      toml::array array;
      for (const auto& group : groups) {
        if (group.id.empty()) {
          continue;
        }
        toml::table item;
        item.insert_or_assign("id", group.id);
        item.insert_or_assign("members", stringArray(group.members));
        item.insert_or_assign("fill", colorSpecToConfigString(group.fill));
        if (group.borderSpecified) {
          item.insert_or_assign(
              "border", group.border.has_value() ? colorSpecToConfigString(*group.border) : std::string{}
          );
        }
        if (group.foreground.has_value()) {
          item.insert_or_assign("foreground", colorSpecToConfigString(*group.foreground));
        }
        item.insert_or_assign("padding", static_cast<double>(group.padding));
        if (group.radius.has_value()) {
          item.insert_or_assign("radius", static_cast<double>(*group.radius));
        }
        item.insert_or_assign("opacity", static_cast<double>(group.opacity));
        array.push_back(std::move(item));
      }
      return array;
    }

    void insertBarFields(toml::table& table, const BarConfig& bar, bool includePosition) {
      if (includePosition) {
        table.insert_or_assign("position", bar.position);
      }
      table.insert_or_assign("enabled", bar.enabled);
      table.insert_or_assign("auto_hide", bar.autoHide);
      table.insert_or_assign("reserve_space", bar.reserveSpace);
      table.insert_or_assign("layer", bar.layer);
      table.insert_or_assign("thickness", static_cast<std::int64_t>(bar.thickness));
      table.insert_or_assign("background_opacity", static_cast<double>(bar.backgroundOpacity));
      table.insert_or_assign("border", colorSpecToConfigString(bar.border));
      table.insert_or_assign("border_width", static_cast<double>(bar.borderWidth));
      table.insert_or_assign("radius", static_cast<std::int64_t>(bar.radius));
      table.insert_or_assign("radius_top_left", static_cast<std::int64_t>(bar.radiusTopLeft));
      table.insert_or_assign("radius_top_right", static_cast<std::int64_t>(bar.radiusTopRight));
      table.insert_or_assign("radius_bottom_left", static_cast<std::int64_t>(bar.radiusBottomLeft));
      table.insert_or_assign("radius_bottom_right", static_cast<std::int64_t>(bar.radiusBottomRight));
      table.insert_or_assign("margin_ends", static_cast<std::int64_t>(bar.marginEnds));
      table.insert_or_assign("margin_edge", static_cast<std::int64_t>(bar.marginEdge));
      table.insert_or_assign("padding", static_cast<std::int64_t>(bar.padding));
      table.insert_or_assign("widget_spacing", static_cast<std::int64_t>(bar.widgetSpacing));
      table.insert_or_assign("shadow", bar.shadow);
      table.insert_or_assign("contact_shadow", bar.contactShadow);
      table.insert_or_assign("panel_overlap", static_cast<std::int64_t>(bar.panelOverlap));
      table.insert_or_assign("scale", static_cast<double>(bar.scale));
      table.insert_or_assign("font_weight", static_cast<std::int64_t>(bar.fontWeight));
      table.insert_or_assign("start", stringArray(bar.startWidgets));
      table.insert_or_assign("center", stringArray(bar.centerWidgets));
      table.insert_or_assign("end", stringArray(bar.endWidgets));
      table.insert_or_assign("capsule", bar.widgetCapsuleDefault);
      table.insert_or_assign("capsule_fill", colorSpecToConfigString(bar.widgetCapsuleFill));
      if (bar.widgetCapsuleForeground.has_value()) {
        table.insert_or_assign("capsule_foreground", colorSpecToConfigString(*bar.widgetCapsuleForeground));
      }
      if (bar.widgetColor.has_value()) {
        table.insert_or_assign("color", colorSpecToConfigString(*bar.widgetColor));
      }
      table.insert_or_assign("capsule_group", capsuleGroupArray(bar.widgetCapsuleGroups));
      table.insert_or_assign("capsule_padding", static_cast<double>(bar.widgetCapsulePadding));
      if (bar.widgetCapsuleRadius.has_value()) {
        table.insert_or_assign("capsule_radius", *bar.widgetCapsuleRadius);
      }
      table.insert_or_assign("capsule_opacity", static_cast<double>(bar.widgetCapsuleOpacity));
      if (bar.widgetCapsuleBorderSpecified) {
        table.insert_or_assign(
            "capsule_border",
            bar.widgetCapsuleBorder.has_value() ? colorSpecToConfigString(*bar.widgetCapsuleBorder) : std::string{}
        );
      }
    }

    BarConfig applyMonitorOverride(const BarConfig& base, const BarMonitorOverride& ovr) {
      BarConfig resolved = base;
      if (ovr.position)
        resolved.position = *ovr.position;
      if (ovr.enabled)
        resolved.enabled = *ovr.enabled;
      if (ovr.autoHide)
        resolved.autoHide = *ovr.autoHide;
      if (ovr.reserveSpace)
        resolved.reserveSpace = *ovr.reserveSpace;
      if (ovr.layer)
        resolved.layer = *ovr.layer;
      if (ovr.thickness)
        resolved.thickness = *ovr.thickness;
      if (ovr.backgroundOpacity)
        resolved.backgroundOpacity = *ovr.backgroundOpacity;
      if (ovr.border)
        resolved.border = *ovr.border;
      if (ovr.borderWidth)
        resolved.borderWidth = *ovr.borderWidth;
      if (ovr.radius) {
        resolved.radius = *ovr.radius;
        resolved.radiusTopLeft = *ovr.radius;
        resolved.radiusTopRight = *ovr.radius;
        resolved.radiusBottomLeft = *ovr.radius;
        resolved.radiusBottomRight = *ovr.radius;
      }
      if (ovr.radiusTopLeft)
        resolved.radiusTopLeft = *ovr.radiusTopLeft;
      if (ovr.radiusTopRight)
        resolved.radiusTopRight = *ovr.radiusTopRight;
      if (ovr.radiusBottomLeft)
        resolved.radiusBottomLeft = *ovr.radiusBottomLeft;
      if (ovr.radiusBottomRight)
        resolved.radiusBottomRight = *ovr.radiusBottomRight;
      if (ovr.marginEnds)
        resolved.marginEnds = *ovr.marginEnds;
      if (ovr.marginEdge)
        resolved.marginEdge = *ovr.marginEdge;
      if (ovr.padding)
        resolved.padding = *ovr.padding;
      if (ovr.widgetSpacing)
        resolved.widgetSpacing = *ovr.widgetSpacing;
      if (ovr.shadow)
        resolved.shadow = *ovr.shadow;
      if (ovr.contactShadow)
        resolved.contactShadow = *ovr.contactShadow;
      if (ovr.panelOverlap)
        resolved.panelOverlap = *ovr.panelOverlap;
      if (ovr.startWidgets)
        resolved.startWidgets = *ovr.startWidgets;
      if (ovr.centerWidgets)
        resolved.centerWidgets = *ovr.centerWidgets;
      if (ovr.endWidgets)
        resolved.endWidgets = *ovr.endWidgets;
      if (ovr.scale)
        resolved.scale = *ovr.scale;
      if (ovr.widgetCapsuleDefault)
        resolved.widgetCapsuleDefault = *ovr.widgetCapsuleDefault;
      if (ovr.widgetCapsuleFill)
        resolved.widgetCapsuleFill = *ovr.widgetCapsuleFill;
      if (ovr.widgetCapsuleBorderSpecified) {
        resolved.widgetCapsuleBorderSpecified = true;
        resolved.widgetCapsuleBorder = ovr.widgetCapsuleBorder;
      }
      if (ovr.widgetCapsuleForeground)
        resolved.widgetCapsuleForeground = *ovr.widgetCapsuleForeground;
      if (ovr.widgetColor)
        resolved.widgetColor = *ovr.widgetColor;
      if (ovr.widgetCapsuleGroups)
        resolved.widgetCapsuleGroups = *ovr.widgetCapsuleGroups;
      if (ovr.widgetCapsulePadding)
        resolved.widgetCapsulePadding = static_cast<float>(*ovr.widgetCapsulePadding);
      if (ovr.widgetCapsuleRadius.has_value()) {
        resolved.widgetCapsuleRadius = *ovr.widgetCapsuleRadius;
      }
      if (ovr.widgetCapsuleOpacity)
        resolved.widgetCapsuleOpacity = static_cast<float>(*ovr.widgetCapsuleOpacity);
      return resolved;
    }

    toml::table barConfigTable(const BarConfig& bar) {
      toml::table table;
      insertBarFields(table, bar, true);

      if (!bar.monitorOverrides.empty()) {
        toml::table monitors;
        for (const auto& ovr : bar.monitorOverrides) {
          if (ovr.match.empty()) {
            continue;
          }
          toml::table monitor = toml::table{};
          monitor.insert_or_assign("match", ovr.match);
          if (ovr.position) {
            monitor.insert_or_assign("position", *ovr.position);
          }
          insertBarFields(monitor, applyMonitorOverride(bar, ovr), false);
          monitors.insert_or_assign(ovr.match, std::move(monitor));
        }
        table.insert_or_assign("monitor", std::move(monitors));
      }
      return table;
    }

    toml::table desktopWidgetsTable(const DesktopWidgetsConfig& desktopWidgets) {
      toml::table table;
      table.insert_or_assign("enabled", desktopWidgets.enabled);
      table.insert_or_assign("schema_version", static_cast<std::int64_t>(desktopWidgets.schemaVersion));

      toml::table grid;
      grid.insert_or_assign("visible", desktopWidgets.grid.visible);
      grid.insert_or_assign("cell_size", static_cast<std::int64_t>(desktopWidgets.grid.cellSize));
      grid.insert_or_assign("major_interval", static_cast<std::int64_t>(desktopWidgets.grid.majorInterval));
      table.insert_or_assign("grid", std::move(grid));

      if (!desktopWidgets.widgets.empty()) {
        toml::array order;
        toml::table widgets;
        for (const auto& widget : desktopWidgets.widgets) {
          if (widget.id.empty()) {
            continue;
          }
          order.push_back(widget.id);
          toml::table item;
          item.insert_or_assign("type", widget.type);
          item.insert_or_assign("output", widget.outputName);
          item.insert_or_assign("cx", static_cast<double>(widget.cx));
          item.insert_or_assign("cy", static_cast<double>(widget.cy));
          item.insert_or_assign("scale", static_cast<double>(widget.scale));
          item.insert_or_assign("rotation", static_cast<double>(widget.rotationRad));
          item.insert_or_assign("enabled", widget.enabled);

          toml::table settings;
          std::vector<std::string> keys;
          keys.reserve(widget.settings.size());
          for (const auto& [key, value] : widget.settings) {
            (void)value;
            keys.push_back(key);
          }
          std::sort(keys.begin(), keys.end());
          for (const auto& key : keys) {
            insertWidgetSettingValue(settings, key, widget.settings.at(key));
          }
          item.insert_or_assign("settings", std::move(settings));
          widgets.insert_or_assign(widget.id, std::move(item));
        }
        table.insert_or_assign("widget_order", std::move(order));
        table.insert_or_assign("widget", std::move(widgets));
      }
      return table;
    }

  } // namespace

  toml::table configToToml(const Config& config) {
    toml::table root;

    root.insert_or_assign("shell", schema::writeTable(config.shell, schema::shellSchema()));
    root.insert_or_assign("wallpaper", schema::writeTable(config.wallpaper, schema::wallpaperSchema()));
    root.insert_or_assign("theme", schema::writeTable(config.theme, schema::themeSchema()));

    root.insert_or_assign("backdrop", schema::writeTable(config.backdrop, schema::backdropSchema()));

    root.insert_or_assign("lockscreen", schema::writeTable(config.lockscreen, schema::lockscreenSchema()));

    root.insert_or_assign("notification", schema::writeTable(config.notification, schema::notificationSchema()));

    root.insert_or_assign("osd", schema::writeTable(config.osd, schema::osdSchema()));

    root.insert_or_assign("system", schema::writeTable(config.system, schema::systemSchema()));

    root.insert_or_assign("weather", schema::writeTable(config.weather, schema::weatherSchema()));
    root.insert_or_assign("calendar", schema::writeTable(config.calendar, schema::calendarSchema()));
    root.insert_or_assign("audio", schema::writeTable(config.audio, schema::audioSchema()));

    root.insert_or_assign("brightness", schema::writeTable(config.brightness, schema::brightnessSchema()));
    root.insert_or_assign("battery", schema::writeTable(config.battery, schema::batterySchema()));

    root.insert_or_assign("nightlight", schema::writeTable(config.nightlight, schema::nightlightSchema()));
    root.insert_or_assign("location", schema::writeTable(config.location, schema::locationSchema()));

    root.insert_or_assign("idle", schema::writeTable(config.idle, schema::idleSchema()));

    root.insert_or_assign("keybinds", schema::writeTable(config.keybinds, schema::keybindsSchema()));

    toml::table barRoot;
    toml::array barOrder;
    for (const auto& bar : config.bars) {
      if (bar.name.empty()) {
        continue;
      }
      barOrder.push_back(bar.name);
      barRoot.insert_or_assign(bar.name, barConfigTable(bar));
    }
    barRoot.insert_or_assign("order", std::move(barOrder));
    root.insert_or_assign("bar", std::move(barRoot));

    root.insert_or_assign("dock", schema::writeTable(config.dock, schema::dockSchema()));
    root.insert_or_assign("desktop_widgets", desktopWidgetsTable(config.desktopWidgets));

    toml::table widgetRoot;
    std::vector<std::string> widgetNames;
    widgetNames.reserve(config.widgets.size());
    for (const auto& [name, widget] : config.widgets) {
      (void)widget;
      widgetNames.push_back(name);
    }
    std::sort(widgetNames.begin(), widgetNames.end());
    for (const auto& name : widgetNames) {
      widgetRoot.insert_or_assign(name, widgetConfigTable(config.widgets.at(name)));
    }
    root.insert_or_assign("widget", std::move(widgetRoot));

    root.insert_or_assign("control_center", schema::writeTable(config.controlCenter, schema::controlCenterSchema()));

    root.insert_or_assign("hooks", schema::writeTable(config.hooks, schema::hooksSchema()));
    return root;
  }

} // namespace config_export
