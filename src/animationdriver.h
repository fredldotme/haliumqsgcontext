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

#ifndef ANIMATIONDRIVER_H
#define ANIMATIONDRIVER_H

#include <QtCore/QAnimationDriver>
#include <QtQuick/QQuickWindow>

class AnimationDriver : public QAnimationDriver
{
    Q_OBJECT

public:
    AnimationDriver(QObject* parent = nullptr);

private:
    void startListening();

    QQuickWindow* m_referenceWindow;
};

#endif // ANIMATIONDRIVER_H