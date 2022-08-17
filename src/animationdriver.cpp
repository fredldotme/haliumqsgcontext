/*
 * Copyright (C) 2022 UBports Foundation
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranties of MERCHANTABILITY,
 * SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "animationdriver.h"

#include <QtCore/qmath.h>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

AnimationDriver::AnimationDriver(QObject* parent)
    : QAnimationDriver(parent)
{
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [=]() {
        startListening();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [=]() {
        startListening();
    });
}

void AnimationDriver::startListening()
{
    if (m_referenceWindow)
        m_referenceWindow = nullptr;

    QWindow* highestRefreshWindow = nullptr;

    for (auto const potentialWindow : QGuiApplication::allWindows()) {
        if (!potentialWindow)
            continue;

        if (!potentialWindow->screen())
            continue;

        if (!highestRefreshWindow) {
            highestRefreshWindow = potentialWindow;
        }

        if (highestRefreshWindow->screen() && highestRefreshWindow->screen()->refreshRate() < potentialWindow->screen()->refreshRate()) {
            highestRefreshWindow = potentialWindow;
        }
    }

    if (!highestRefreshWindow) {
        return;
    }

    QQuickWindow *window = qobject_cast<QQuickWindow *>(highestRefreshWindow);
    if (!window) {
        return;
    }

    m_referenceWindow = window;
    connect(window, &QQuickWindow::frameSwapped, this, &AnimationDriver::advance, Qt::DirectConnection);
}
