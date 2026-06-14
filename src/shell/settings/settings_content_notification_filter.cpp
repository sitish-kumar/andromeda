#include "config/config_types.h"
#include "i18n/i18n.h"
#include "notification/notification_filter.h"
#include "shell/settings/settings_content.h"
#include "shell/settings/settings_content_common.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/input.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <functional>
#include <string>
#include <utility>

namespace settings {

  namespace {

    void addToggleRow(
        Flex& parent, float scale, std::string label, bool checked, const std::function<void(bool)>& onChange
    ) {
      auto row = ui::row({
          .align = FlexAlign::Center,
          .justify = FlexJustify::SpaceBetween,
          .gap = Style::spaceSm * scale,
          .minHeight = Style::controlHeightSm * scale,
      });
      row->addChild(makeLabel(
          std::move(label), Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Normal
      ));
      row->addChild(
          ui::toggle({
              .checked = checked,
              .scale = scale,
              .onChange = onChange,
          })
      );
      parent.addChild(std::move(row));
    }

  } // namespace

  void buildNotificationFilterEntryDetailContent(
      Flex& parent, SettingsContentContext& ctx, NotificationFilterConfig& row, const std::function<void()>& persist
  ) {
    const float scale = ctx.scale;

    auto body = ui::column({
        .align = FlexAlign::Stretch,
        .gap = Style::spaceMd * scale,
    });

    auto matchBlock = ui::column(
        {.align = FlexAlign::Stretch, .gap = Style::spaceXs * scale},
        makeLabel(
            i18n::tr("settings.notifications.filter.match-label"), Style::fontSizeCaption * scale,
            colorSpecFromRole(ColorRole::OnSurfaceVariant), FontWeight::Normal
        )
    );
    Input* matchPtr = nullptr;
    auto matchInput = ui::input({
        .out = &matchPtr,
        .value = row.match,
        .placeholder = i18n::tr("settings.notifications.filter.match-placeholder"),
        .fontSize = Style::fontSizeBody * scale,
        .controlHeight = Style::controlHeight * scale,
        .horizontalPadding = Style::spaceSm * scale,
    });
    const auto commitMatch = [&row, persist, matchPtr]() {
      row.match = normalizeNotificationMatchToken(matchPtr->value());
      if (row.match.empty()) {
        matchPtr->setInvalid(true);
        return;
      }
      matchPtr->setInvalid(false);
      matchPtr->setValue(row.match);
      persist();
    };
    matchInput->setOnChange([matchPtr](const std::string& /*t*/) { matchPtr->setInvalid(false); });
    matchInput->setOnSubmit([commitMatch](const std::string& /*text*/) { commitMatch(); });
    matchInput->setOnFocusLoss(commitMatch);
    matchBlock->addChild(std::move(matchInput));
    body->addChild(std::move(matchBlock));

    auto flagsBlock = ui::column(
        {.align = FlexAlign::Stretch, .gap = Style::spaceSm * scale},
        makeLabel(
            i18n::tr("settings.notifications.filter.flags-label"), Style::fontSizeCaption * scale,
            colorSpecFromRole(ColorRole::OnSurfaceVariant), FontWeight::Normal
        )
    );
    addToggleRow(
        *flagsBlock, scale, i18n::tr("settings.notifications.filter.show-toast"), row.showToast,
        [&row, persist](bool value) {
          row.showToast = value;
          persist();
        }
    );
    addToggleRow(
        *flagsBlock, scale, i18n::tr("settings.notifications.filter.save-history"), row.saveHistory,
        [&row, persist](bool value) {
          row.saveHistory = value;
          persist();
        }
    );
    addToggleRow(
        *flagsBlock, scale, i18n::tr("settings.notifications.filter.play-sound"), row.playSound,
        [&row, persist](bool value) {
          row.playSound = value;
          persist();
        }
    );
    addToggleRow(
        *flagsBlock, scale, i18n::tr("settings.notifications.filter.allow-critical"), row.allowCritical,
        [&row, persist](bool value) {
          row.allowCritical = value;
          persist();
        }
    );
    body->addChild(std::move(flagsBlock));

    parent.addChild(std::move(body));

    auto actions = ui::row(
        {.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true},
        ui::button({
            .text = i18n::tr("common.actions.apply"),
            .glyph = "check",
            .fontSize = Style::fontSizeBody * scale,
            .glyphSize = Style::fontSizeBody * scale,
            .variant = ButtonVariant::Default,
            .minHeight = Style::controlHeight * scale,
            .paddingV = Style::spaceSm * scale,
            .paddingH = Style::spaceMd * scale,
            .radius = Style::scaledRadiusMd(scale),
            .flexGrow = 1.0f,
            .onClick = [commitMatch, applyHostedEditor = ctx.afterNotificationFilterApply,
                        closeHostedEditor = ctx.closeHostedEditor]() {
              commitMatch();
              if (applyHostedEditor) {
                applyHostedEditor();
              }
              if (closeHostedEditor) {
                closeHostedEditor();
              }
            },
        })
    );
    parent.addChild(std::move(actions));
  }

} // namespace settings
