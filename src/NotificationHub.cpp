#include "NotificationHub.h"

namespace icloud {

NotificationHub::NotificationHub(QObject *parent) : QObject(parent) {}

void NotificationHub::post(const QString &kind,
                           const QString &title,
                           const QString &body) {
    emit requested(kind, title, body);
}

} // namespace icloud
