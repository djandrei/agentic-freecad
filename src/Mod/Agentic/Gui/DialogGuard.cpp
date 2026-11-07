// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <QAbstractButton>
#include <QDialog>
#include <QEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>

#include <Base/Console.h>

#include <Mod/Agentic/App/Runtime.h>

#include "DialogGuard.h"

namespace Agentic
{

DialogGuard::DialogGuard(QObject* parent)
    : QObject(parent)
{}

void DialogGuard::install()
{
    static DialogGuard* guard = nullptr;
    if (!guard && qApp) {
        guard = new DialogGuard(qApp);
        qApp->installEventFilter(guard);
    }
}

bool DialogGuard::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::Show) {
        return false;
    }
    auto dialog = qobject_cast<QDialog*>(watched);
    if (!dialog || !dialog->isModal()) {
        return false;
    }
    Runtime& runtime = Runtime::instance();
    if (!runtime.config().enabled && runtime.currentTool().empty()) {
        return false;  // a person is operating the classic GUI
    }

    QPointer<QDialog> safe(dialog);
    QTimer::singleShot(0, dialog, [safe, &runtime]() {
        if (!safe) {
            return;
        }
        nlohmann::json note = {
            {"class", safe->metaObject()->className()},
            {"title", safe->windowTitle().toStdString()},
        };
        if (auto box = qobject_cast<QMessageBox*>(safe.data())) {
            note["text"] = box->text().toStdString();
            if (!box->informativeText().isEmpty()) {
                note["informativeText"] = box->informativeText().toStdString();
            }
            // The default button, else the escape button, else the button
            // that declines (No, Cancel, Close): never guess "yes" for a person.
            QAbstractButton* button = box->defaultButton();
            if (!button) {
                button = box->escapeButton();
            }
            if (!button) {
                for (QAbstractButton* candidate : box->buttons()) {
                    const auto role = box->buttonRole(candidate);
                    if (role == QMessageBox::NoRole || role == QMessageBox::RejectRole) {
                        button = candidate;
                        break;
                    }
                }
            }
            if (button) {
                note["answer"] = button->text().remove(QLatin1Char('&')).toStdString();
                button->click();
            }
            else {
                note["answer"] = "closed";
                box->reject();
            }
        }
        else if (qobject_cast<QFileDialog*>(safe.data())) {
            note["answer"] = "cancelled";
            safe->reject();
        }
        else {
            note["answer"] = "rejected";
            safe->reject();
        }
        Base::Console().log("Agentic: answered modal dialog {}\n", note.dump());
        runtime.noteDialog(std::move(note));
    });
    return false;
}

}  // namespace Agentic
