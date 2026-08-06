#include <gtk/gtk.h>

#include "vendor/json.hpp"

#include <iostream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

struct OutputItem {
  std::string name;
  std::string description;
  int width = 0;
  int height = 0;
};

struct WindowItem {
  std::string identifier;
  std::string appId;
  std::string title;
};

enum class RowKind {
  Monitor = 1,
  Window = 2,
};

struct AppState {
  bool multiple = false;
  bool showMonitors = false;
  bool showWindows = false;
  bool responding = false;
  bool updatingSelection = false;
  std::vector<OutputItem> outputs;
  std::vector<WindowItem> windows;
  GtkApplication* app = nullptr;
  GtkWidget* window = nullptr;
  GtkWidget* outputList = nullptr;
  GtkWidget* windowList = nullptr;
  GtkWidget* shareButton = nullptr;
};

std::string readStdin()
{
  std::string input;
  char buffer[4096];
  while (std::cin.good()) {
    std::cin.read(buffer, sizeof(buffer));
    input.append(buffer, static_cast<size_t>(std::cin.gcount()));
  }
  return input;
}

std::string jsonString(const json& object, const char* key)
{
  const auto it = object.find(key);
  if (it == object.end() || !it->is_string()) {
    return {};
  }
  return it->get<std::string>();
}

int jsonInt(const json& object, const char* key)
{
  const auto it = object.find(key);
  if (it == object.end() || !it->is_number_integer()) {
    return 0;
  }
  return it->get<int>();
}

AppState parseRequest(const std::string& input)
{
  AppState state;

  try {
    const json request = json::parse(input);
    if (!request.is_object()) {
      return state;
    }

    const auto multiple = request.find("multiple");
    state.multiple = multiple != request.end() && multiple->is_boolean() && multiple->get<bool>();

    const auto types = request.find("types");
    if (types != request.end() && types->is_array()) {
      for (const json& type : *types) {
        if (!type.is_string()) {
          continue;
        }
        const std::string value = type.get<std::string>();
        state.showMonitors = state.showMonitors || value == "monitor";
        state.showWindows = state.showWindows || value == "window";
      }
    }

    const auto outputs = request.find("outputs");
    if (outputs != request.end() && outputs->is_array()) {
      for (const json& output : *outputs) {
        if (!output.is_object()) {
          continue;
        }
        state.outputs.push_back({
            .name = jsonString(output, "name"),
            .description = jsonString(output, "description"),
            .width = jsonInt(output, "width"),
            .height = jsonInt(output, "height"),
        });
      }
    }

    const auto windows = request.find("windows");
    if (windows != request.end() && windows->is_array()) {
      for (const json& window : *windows) {
        if (!window.is_object()) {
          continue;
        }
        state.windows.push_back({
            .identifier = jsonString(window, "identifier"),
            .appId = jsonString(window, "app_id"),
            .title = jsonString(window, "title"),
        });
      }
    }
  } catch (const json::exception& error) {
    std::cerr << "umbriel-share-picker: invalid request JSON: " << error.what() << '\n';
  }

  return state;
}

void printResponse(const json& response)
{
  std::cout << response.dump() << '\n' << std::flush;
}

void quitAfterResponse(AppState& state, const json& response)
{
  if (state.responding) {
    return;
  }
  state.responding = true;
  printResponse(response);
  if (state.app != nullptr) {
    g_application_quit(G_APPLICATION(state.app));
  }
}

void cancel(AppState& state)
{
  quitAfterResponse(state, json{{"selections", json::array()}});
}

GList* selectedRows(GtkWidget* list)
{
  if (list == nullptr) {
    return nullptr;
  }
  return gtk_list_box_get_selected_rows(GTK_LIST_BOX(list));
}

bool hasSelection(GtkWidget* list)
{
  GList* rows = selectedRows(list);
  const bool selected = rows != nullptr;
  g_list_free(rows);
  return selected;
}

void updateShareButton(AppState& state)
{
  if (state.shareButton == nullptr) {
    return;
  }
  gtk_widget_set_sensitive(state.shareButton, hasSelection(state.outputList) || hasSelection(state.windowList));
}

void unselectList(GtkWidget* list)
{
  if (list != nullptr) {
    gtk_list_box_unselect_all(GTK_LIST_BOX(list));
  }
}

void onSelectedRowsChanged(GtkListBox* list, gpointer userData)
{
  auto* state = static_cast<AppState*>(userData);
  if (state->updatingSelection) {
    updateShareButton(*state);
    return;
  }

  if (!state->multiple && hasSelection(GTK_WIDGET(list))) {
    state->updatingSelection = true;
    if (GTK_WIDGET(list) != state->outputList) {
      unselectList(state->outputList);
    }
    if (GTK_WIDGET(list) != state->windowList) {
      unselectList(state->windowList);
    }
    state->updatingSelection = false;
  }

  updateShareButton(*state);
}

std::string displayOrFallback(const std::string& value, const char* fallback)
{
  return value.empty() ? std::string(fallback) : value;
}

GtkWidget* makeLabel(const std::string& text, bool bold, bool dim)
{
  GtkWidget* label = gtk_label_new(nullptr);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);

  if (bold) {
    char* escaped = g_markup_escape_text(text.c_str(), -1);
    const std::string markup = std::string("<b>") + escaped + "</b>";
    g_free(escaped);
    gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
  } else {
    gtk_label_set_text(GTK_LABEL(label), text.c_str());
  }

  if (dim) {
    gtk_widget_add_css_class(label, "dim-label");
  }

  return label;
}

GtkWidget* makeOutputRow(const OutputItem& output, guint index)
{
  GtkWidget* row = gtk_list_box_row_new();
  g_object_set_data(G_OBJECT(row), "xdpu-kind", GINT_TO_POINTER(static_cast<int>(RowKind::Monitor)));
  g_object_set_data(G_OBJECT(row), "xdpu-index", GUINT_TO_POINTER(index));

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_top(box, 10);
  gtk_widget_set_margin_bottom(box, 10);
  gtk_widget_set_margin_start(box, 12);
  gtk_widget_set_margin_end(box, 12);

  GtkWidget* textBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_hexpand(textBox, TRUE);
  gtk_box_append(GTK_BOX(textBox), makeLabel(displayOrFallback(output.name, "Unnamed screen"), true, false));
  gtk_box_append(GTK_BOX(textBox), makeLabel(output.description, false, true));

  const std::string detail = std::to_string(output.width) + "×" + std::to_string(output.height);
  GtkWidget* detailLabel = makeLabel(detail, false, true);
  gtk_label_set_xalign(GTK_LABEL(detailLabel), 1.0F);

  gtk_box_append(GTK_BOX(box), textBox);
  gtk_box_append(GTK_BOX(box), detailLabel);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
  return row;
}

GtkWidget* makeWindowRow(const WindowItem& window, guint index)
{
  GtkWidget* row = gtk_list_box_row_new();
  g_object_set_data(G_OBJECT(row), "xdpu-kind", GINT_TO_POINTER(static_cast<int>(RowKind::Window)));
  g_object_set_data(G_OBJECT(row), "xdpu-index", GUINT_TO_POINTER(index));

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_margin_top(box, 10);
  gtk_widget_set_margin_bottom(box, 10);
  gtk_widget_set_margin_start(box, 12);
  gtk_widget_set_margin_end(box, 12);

  gtk_box_append(GTK_BOX(box), makeLabel(displayOrFallback(window.title, "Untitled window"), false, false));
  gtk_box_append(GTK_BOX(box), makeLabel(window.appId, false, true));

  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
  return row;
}

GtkWidget* makePlaceholder(const char* text)
{
  GtkWidget* label = gtk_label_new(text);
  gtk_widget_set_margin_top(label, 24);
  gtk_widget_set_margin_bottom(label, 24);
  gtk_widget_add_css_class(label, "dim-label");
  return label;
}

GtkWidget* makeListBox(AppState& state, RowKind kind)
{
  GtkWidget* list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), state.multiple ? GTK_SELECTION_MULTIPLE : GTK_SELECTION_SINGLE);
  gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), TRUE);
  g_signal_connect(list, "selected-rows-changed", G_CALLBACK(onSelectedRowsChanged), &state);

  if (kind == RowKind::Monitor) {
    gtk_list_box_set_placeholder(GTK_LIST_BOX(list), makePlaceholder("No screens available"));
    for (size_t i = 0; i < state.outputs.size(); ++i) {
      gtk_list_box_append(GTK_LIST_BOX(list), makeOutputRow(state.outputs[i], static_cast<guint>(i)));
    }
  } else {
    gtk_list_box_set_placeholder(GTK_LIST_BOX(list), makePlaceholder("No windows available"));
    for (size_t i = 0; i < state.windows.size(); ++i) {
      gtk_list_box_append(GTK_LIST_BOX(list), makeWindowRow(state.windows[i], static_cast<guint>(i)));
    }
  }

  return list;
}

GtkWidget* makeScrolledList(GtkWidget* list)
{
  GtkWidget* scrolled = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scrolled, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list);
  return scrolled;
}

void appendSelectedRows(AppState& state, GtkWidget* list, json& selections)
{
  GList* rows = selectedRows(list);
  for (GList* node = rows; node != nullptr; node = node->next) {
    auto* row = GTK_WIDGET(node->data);
    const auto kind = static_cast<RowKind>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "xdpu-kind")));
    const guint index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "xdpu-index"));

    if (kind == RowKind::Monitor && index < state.outputs.size()) {
      selections.push_back({{"kind", "monitor"}, {"output", state.outputs[index].name}});
    } else if (kind == RowKind::Window && index < state.windows.size()) {
      selections.push_back({{"kind", "window"}, {"identifier", state.windows[index].identifier}});
    }

    if (!state.multiple && !selections.empty()) {
      break;
    }
  }
  g_list_free(rows);
}

void share(AppState& state)
{
  json selections = json::array();
  appendSelectedRows(state, state.outputList, selections);
  if (state.multiple || selections.empty()) {
    appendSelectedRows(state, state.windowList, selections);
  }
  quitAfterResponse(state, json{{"selections", std::move(selections)}});
}

void onShareClicked(GtkButton*, gpointer userData)
{
  share(*static_cast<AppState*>(userData));
}

void onCancelClicked(GtkButton*, gpointer userData)
{
  cancel(*static_cast<AppState*>(userData));
}

gboolean onCloseRequest(GtkWindow*, gpointer userData)
{
  cancel(*static_cast<AppState*>(userData));
  return TRUE;
}

gboolean onKeyPressed(GtkEventControllerKey*, guint keyval, guint, GdkModifierType, gpointer userData)
{
  auto* state = static_cast<AppState*>(userData);

  if (keyval == GDK_KEY_Escape) {
    cancel(*state);
    return TRUE;
  }

  if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
    if (state->shareButton != nullptr && gtk_widget_get_sensitive(state->shareButton)) {
      share(*state);
    }
    return TRUE;
  }

  return FALSE;
}

GtkWidget* makeContent(AppState& state, GtkWidget* header)
{
  GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_vexpand(body, TRUE);

  if (state.showMonitors) {
    state.outputList = makeListBox(state, RowKind::Monitor);
  }
  if (state.showWindows) {
    state.windowList = makeListBox(state, RowKind::Window);
  }

  if (state.showMonitors && state.showWindows) {
    GtkWidget* stack = gtk_stack_new();
    GtkWidget* switcher = gtk_stack_switcher_new();
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), switcher);
    gtk_stack_add_titled(GTK_STACK(stack), makeScrolledList(state.outputList), "screens", "Screens");
    gtk_stack_add_titled(GTK_STACK(stack), makeScrolledList(state.windowList), "windows", "Windows");
    gtk_box_append(GTK_BOX(body), stack);
  } else if (state.showMonitors) {
    gtk_box_append(GTK_BOX(body), makeScrolledList(state.outputList));
  } else if (state.showWindows) {
    gtk_box_append(GTK_BOX(body), makeScrolledList(state.windowList));
  } else {
    GtkWidget* placeholder = makePlaceholder("No share source types requested");
    gtk_widget_set_vexpand(placeholder, TRUE);
    gtk_widget_set_valign(placeholder, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(body), placeholder);
  }

  GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(buttons, GTK_ALIGN_END);
  gtk_widget_set_margin_top(buttons, 12);
  gtk_widget_set_margin_bottom(buttons, 12);
  gtk_widget_set_margin_start(buttons, 12);
  gtk_widget_set_margin_end(buttons, 12);

  GtkWidget* cancelButton = gtk_button_new_with_label("Cancel");
  state.shareButton = gtk_button_new_with_label("Share");
  gtk_widget_add_css_class(state.shareButton, "suggested-action");
  gtk_widget_set_sensitive(state.shareButton, FALSE);

  g_signal_connect(cancelButton, "clicked", G_CALLBACK(onCancelClicked), &state);
  g_signal_connect(state.shareButton, "clicked", G_CALLBACK(onShareClicked), &state);

  gtk_box_append(GTK_BOX(buttons), cancelButton);
  gtk_box_append(GTK_BOX(buttons), state.shareButton);
  gtk_box_append(GTK_BOX(content), body);
  gtk_box_append(GTK_BOX(content), buttons);
  return content;
}

void onActivate(GtkApplication* app, gpointer userData)
{
  auto* state = static_cast<AppState*>(userData);
  state->app = app;

  GtkWidget* window = gtk_application_window_new(app);
  state->window = window;
  gtk_window_set_title(GTK_WINDOW(window), "Share");
  gtk_window_set_default_size(GTK_WINDOW(window), 480, 420);

  GtkWidget* header = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(window), header);
  gtk_window_set_child(GTK_WINDOW(window), makeContent(*state, header));
  gtk_window_set_default_widget(GTK_WINDOW(window), state->shareButton);

  GtkEventController* keyController = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(keyController, GTK_PHASE_CAPTURE);
  g_signal_connect(keyController, "key-pressed", G_CALLBACK(onKeyPressed), state);
  gtk_widget_add_controller(window, keyController);

  g_signal_connect(window, "close-request", G_CALLBACK(onCloseRequest), state);
  gtk_window_present(GTK_WINDOW(window));
}

} // namespace

int main(int argc, char** argv)
{
  AppState state = parseRequest(readStdin());

  gtk_init();

  GtkApplication* app = gtk_application_new("dev.noctalia.UmbrielSharePicker", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(onActivate), &state);
  const int status = g_application_run(G_APPLICATION(app), argc, argv);
  if (!state.responding) {
    cancel(state);
  }
  g_object_unref(app);

  (void)status;
  return 0;
}
