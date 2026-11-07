// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <map>
#include <vector>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTextEdit>
#include <QToolButton>

#include <Base/Quantity.h>
#include <Gui/Control.h>
#include <Gui/QuantitySpinBox.h>
#include <Gui/TaskView/TaskDialog.h>

#include "TaskPanelEngine.h"

using json = nlohmann::json;

namespace Agentic
{

namespace
{

struct Field
{
    std::string id;
    QWidget* widget {nullptr};
    std::string kind;
};

std::string labelFor(QWidget* widget)
{
    for (QLabel* label : widget->window()->findChildren<QLabel*>()) {
        if (label->buddy() == widget) {
            return label->text().remove(QLatin1Char('&')).toStdString();
        }
    }
    if (!widget->accessibleName().isEmpty()) {
        return widget->accessibleName().toStdString();
    }
    if (auto button = qobject_cast<QAbstractButton*>(widget)) {
        return button->text().remove(QLatin1Char('&')).toStdString();
    }
    if (auto group = qobject_cast<QGroupBox*>(widget)) {
        return group->title().toStdString();
    }
    if (!widget->toolTip().isEmpty()) {
        return widget->toolTip().toStdString();
    }
    if (auto edit = qobject_cast<QLineEdit*>(widget)) {
        return edit->placeholderText().toStdString();
    }
    return {};
}

std::string kindOf(QWidget* w)
{
    if (qobject_cast<Gui::QuantitySpinBox*>(w)) {
        return "quantity";
    }
    if (qobject_cast<QDoubleSpinBox*>(w)) {
        return "number";
    }
    if (qobject_cast<QSpinBox*>(w) || qobject_cast<QSlider*>(w)) {
        return "integer";
    }
    if (qobject_cast<QComboBox*>(w)) {
        return "choice";
    }
    if (qobject_cast<QCheckBox*>(w) || qobject_cast<QRadioButton*>(w)) {
        return "boolean";
    }
    if (auto group = qobject_cast<QGroupBox*>(w)) {
        return group->isCheckable() ? "boolean" : "";
    }
    if (auto edit = qobject_cast<QLineEdit*>(w)) {
        // The line edit inside a spin box or combo box is not a field of its own.
        QWidget* parent = edit->parentWidget();
        if (qobject_cast<QAbstractSpinBox*>(parent) || qobject_cast<QComboBox*>(parent)) {
            return "";
        }
        return "text";
    }
    if (qobject_cast<QPlainTextEdit*>(w) || qobject_cast<QTextEdit*>(w)) {
        return "text";
    }
    if (qobject_cast<QListWidget*>(w)) {
        return "list";
    }
    if (auto button = qobject_cast<QAbstractButton*>(w)) {
        if (qobject_cast<QPushButton*>(w) || qobject_cast<QToolButton*>(w)) {
            // Buttons inside the dialog button box are accept/reject, not fields.
            if (qobject_cast<QDialogButtonBox*>(button->parentWidget())) {
                return "";
            }
            return button->isCheckable() ? "toggle" : "button";
        }
    }
    return "";
}

json valueOf(QWidget* w, const std::string& kind)
{
    if (kind == "quantity") {
        auto box = qobject_cast<Gui::QuantitySpinBox*>(w);
        json out = {
            {"value", box->rawValue()},
            {"unit", box->unitText().toStdString()},
            {"text", box->text().toStdString()}
        };
        if (!box->expressionText().isEmpty()) {
            out["expression"] = box->expressionText().toStdString();
        }
        return out;
    }
    if (auto box = qobject_cast<QDoubleSpinBox*>(w)) {
        return box->value();
    }
    if (auto box = qobject_cast<QSpinBox*>(w)) {
        return box->value();
    }
    if (auto slider = qobject_cast<QSlider*>(w)) {
        return slider->value();
    }
    if (auto combo = qobject_cast<QComboBox*>(w)) {
        return combo->currentText().toStdString();
    }
    if (auto check = qobject_cast<QAbstractButton*>(w)) {
        return check->isChecked();
    }
    if (auto group = qobject_cast<QGroupBox*>(w)) {
        return group->isChecked();
    }
    if (auto edit = qobject_cast<QLineEdit*>(w)) {
        return edit->text().toStdString();
    }
    if (auto edit = qobject_cast<QPlainTextEdit*>(w)) {
        return edit->toPlainText().toStdString();
    }
    if (auto edit = qobject_cast<QTextEdit*>(w)) {
        return edit->toPlainText().toStdString();
    }
    if (auto list = qobject_cast<QListWidget*>(w)) {
        json selected = json::array();
        for (QListWidgetItem* item : list->selectedItems()) {
            selected.push_back(item->text().toStdString());
        }
        return selected;
    }
    return json();
}

json optionsOf(QWidget* w)
{
    if (auto combo = qobject_cast<QComboBox*>(w)) {
        json items = json::array();
        for (int i = 0; i < combo->count(); ++i) {
            items.push_back(combo->itemText(i).toStdString());
        }
        return items;
    }
    if (auto list = qobject_cast<QListWidget*>(w)) {
        json items = json::array();
        for (int i = 0; i < list->count(); ++i) {
            items.push_back(list->item(i)->text().toStdString());
        }
        return items;
    }
    return json();
}

std::vector<Field> collectFields(Gui::TaskView::TaskDialog* dialog)
{
    std::vector<Field> fields;
    std::map<std::string, int> seen;
    for (QWidget* content : dialog->getDialogContent()) {
        QList<QWidget*> widgets = content->findChildren<QWidget*>();
        widgets.prepend(content);
        for (QWidget* w : widgets) {
            const std::string kind = kindOf(w);
            // Widgets on an inactive page of a stacked widget, or in a collapsed
            // box, are not part of what a person sees and can edit.
            const bool visible = (w == content) ? !w->isHidden() : w->isVisibleTo(content);
            if (kind.empty() || !visible) {
                continue;
            }
            // The header of a task box toggles it open; it is not an input.
            if (
                QString::fromLatin1(w->metaObject()->className()).contains(QLatin1String("ActionLabel"))
            ) {
                continue;
            }
            std::string id = w->objectName().toStdString();
            if (id.empty() || id.starts_with("qt_")) {
                id = std::string(w->metaObject()->className());
            }
            const int n = seen[id]++;
            if (n > 0) {
                id += "#" + std::to_string(n + 1);
            }
            fields.push_back({id, w, kind});
        }
    }
    return fields;
}

void setField(const Field& field, const json& value)
{
    QWidget* w = field.widget;
    if (field.kind == "quantity") {
        auto box = qobject_cast<Gui::QuantitySpinBox*>(w);
        if (value.is_string()) {
            box->setValue(Base::Quantity::parse(value.get<std::string>()));
        }
        else if (value.is_object() && value.contains("value")) {
            const std::string unit = value.value("unit", std::string());
            box->setValue(
                Base::Quantity::parse(std::to_string(value["value"].get<double>()) + " " + unit)
            );
        }
        else {
            box->setValue(value.get<double>());
        }
        return;
    }
    if (auto box = qobject_cast<QDoubleSpinBox*>(w)) {
        box->setValue(value.get<double>());
        return;
    }
    if (auto box = qobject_cast<QSpinBox*>(w)) {
        box->setValue(value.get<int>());
        return;
    }
    if (auto slider = qobject_cast<QSlider*>(w)) {
        slider->setValue(value.get<int>());
        return;
    }
    if (auto combo = qobject_cast<QComboBox*>(w)) {
        if (value.is_number_integer()) {
            combo->setCurrentIndex(value.get<int>());
        }
        else {
            const int index = combo->findText(QString::fromStdString(value.get<std::string>()));
            if (index < 0) {
                throw std::invalid_argument(field.id + ": no option " + value.dump());
            }
            combo->setCurrentIndex(index);
        }
        // Panels often listen to activated(), which Qt emits only for a user
        // choice (Part_Primitives switches its page that way); emit it as a
        // click on the item would.
        Q_EMIT combo->activated(combo->currentIndex());
        Q_EMIT combo->textActivated(combo->currentText());
        return;
    }
    if (field.kind == "button") {
        qobject_cast<QAbstractButton*>(w)->click();
        return;
    }
    if (auto button = qobject_cast<QAbstractButton*>(w)) {
        if (button->isChecked() != value.get<bool>()) {
            button->click();
        }
        return;
    }
    if (auto group = qobject_cast<QGroupBox*>(w)) {
        group->setChecked(value.get<bool>());
        return;
    }
    if (auto edit = qobject_cast<QLineEdit*>(w)) {
        edit->setText(QString::fromStdString(value.get<std::string>()));
        Q_EMIT edit->editingFinished();
        return;
    }
    if (auto edit = qobject_cast<QPlainTextEdit*>(w)) {
        edit->setPlainText(QString::fromStdString(value.get<std::string>()));
        return;
    }
    if (auto edit = qobject_cast<QTextEdit*>(w)) {
        edit->setPlainText(QString::fromStdString(value.get<std::string>()));
        return;
    }
    if (auto list = qobject_cast<QListWidget*>(w)) {
        list->clearSelection();
        for (const auto& wanted : value) {
            const auto matches = list->findItems(
                QString::fromStdString(wanted.get<std::string>()),
                Qt::MatchExactly
            );
            if (matches.isEmpty()) {
                throw std::invalid_argument(field.id + ": no item " + wanted.dump());
            }
            for (QListWidgetItem* item : matches) {
                item->setSelected(true);
            }
            list->setCurrentItem(matches.first());
        }
        return;
    }
    throw std::invalid_argument(field.id + ": cannot set a " + field.kind + " field");
}

json buttonsOf(Gui::TaskView::TaskDialog* dialog)
{
    json out = json::array();
    const QDialogButtonBox::StandardButtons buttons = dialog->getStandardButtons();
    const std::pair<QDialogButtonBox::StandardButton, const char*> known[] = {
        {QDialogButtonBox::Ok, "Ok"},
        {QDialogButtonBox::Cancel, "Cancel"},
        {QDialogButtonBox::Apply, "Apply"},
        {QDialogButtonBox::Close, "Close"},
        {QDialogButtonBox::Help, "Help"},
    };
    for (const auto& [flag, name] : known) {
        if (buttons & flag) {
            out.push_back(name);
        }
    }
    return out;
}

}  // namespace

Gui::TaskView::TaskDialog* TaskPanelEngine::requireDialog()
{
    Gui::TaskView::TaskDialog* dialog = Gui::Control().activeDialog();
    if (!dialog) {
        throw std::runtime_error("no task panel is open");
    }
    return dialog;
}

json TaskPanelEngine::describeDialog(Gui::TaskView::TaskDialog* dialog)
{
    json fields = json::array();
    for (const Field& field : collectFields(dialog)) {
        json entry = {
            {"id", field.id},
            {"kind", field.kind},
            {"label", labelFor(field.widget)},
            {"enabled", field.widget->isEnabled()},
            {"value", valueOf(field.widget, field.kind)},
        };
        json options = optionsOf(field.widget);
        if (!options.is_null()) {
            entry["options"] = options;
        }
        if (!field.widget->toolTip().isEmpty()) {
            entry["toolTip"] = field.widget->toolTip().toStdString();
        }
        fields.push_back(entry);
    }
    json boxes = json::array();
    for (QWidget* content : dialog->getDialogContent()) {
        boxes.push_back(content->windowTitle().toStdString());
    }
    return {
        {"open", true},
        {"document", dialog->getDocumentName()},
        {"boxes", boxes},
        {"buttons", buttonsOf(dialog)},
        {"fields", fields},
    };
}

json TaskPanelEngine::describe()
{
    Gui::TaskView::TaskDialog* dialog = Gui::Control().activeDialog();
    if (!dialog) {
        return json();
    }
    return describeDialog(dialog);
}

json TaskPanelEngine::set(const json& values)
{
    Gui::TaskView::TaskDialog* dialog = requireDialog();
    const std::vector<Field> fields = collectFields(dialog);
    json changed = json::array();
    for (const auto& [id, value] : values.items()) {
        auto it = std::find_if(fields.begin(), fields.end(), [&id](const Field& f) {
            return f.id == id;
        });
        if (it == fields.end()) {
            throw std::invalid_argument("no field with id " + id + " (call task_get for the list)");
        }
        setField(*it, value);
        changed.push_back(id);
    }
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    json out = describe();
    out["changed"] = changed;
    return out;
}

json TaskPanelEngine::accept()
{
    requireDialog();
    Gui::Control().accept();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    return {{"accepted", true}, {"taskPanel", describe()}};
}

json TaskPanelEngine::reject()
{
    requireDialog();
    Gui::Control().reject();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    return {{"rejected", true}, {"taskPanel", describe()}};
}

}  // namespace Agentic
