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

#ifndef CONTEXTPLUGIN_H
#define CONTEXTPLUGIN_H

#include <private/qsgcontext_p.h>
#include <private/qsgcontextplugin_p.h>

#include <qplugin.h>

#include "context.h"

class LomiriContextPlugin : public QSGContextPlugin
{
    Q_OBJECT

    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QSGContextFactoryInterface" FILE "haliumqsgcontext.json")

public:
    LomiriContextPlugin(QObject *parent = 0);

    QStringList keys() const;
    QSGContext* create(const QString &key) const;
    Flags flags(const QString &) const { return 0; }

#if 0
    virtual QQuickTextureFactory *createTextureFactoryFromImage(const QImage &image) override;
#endif

    static Context* instance;
};

#endif // CONTEXTPLUGIN_H
