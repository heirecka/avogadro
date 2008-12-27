/**********************************************************************
  PluginManager - Class to handle dynamic loading/unloading of plugins

  Copyright (C) 2008 Donald Ephraim Curtis
  Copyright (C) 2008 Tim Vandermeersch
  Copyright (C) 2008 Marcus D. Hanwell

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.sourceforge.net/>

  Avogadro is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Avogadro is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.
 **********************************************************************/

#include <config.h>
#include "pluginmanager.h"
#include "plugindialog.h"

#include <avogadro/engine.h>
#include <avogadro/tool.h>
#include <avogadro/extension.h>
#include <avogadro/color.h>

// Include static headers
#include "engines/bsdyengine.h"
#include "colors/elementcolor.h"

#include <QSettings>
#include <QDir>
#include <QList>
#include <QStringList>
#include <QPluginLoader>
#include <QDebug>

namespace Avogadro {

  class PluginItemPrivate
  {
    public:
      QString name;
      QString description;
      QString fileName;
      QString absoluteFilePath;
      Plugin::Type type;
      PluginFactory *factory;
      bool enabled;
  };

  PluginItem::PluginItem() : d(new PluginItemPrivate)
  {
    d->type = Plugin::OtherType;
    d->enabled = false;
  }

  PluginItem::PluginItem(
      const QString &name,
      const QString &description,
      Plugin::Type type,
      const QString &fileName,
      const QString &filePath,
      PluginFactory *factory,
      bool enabled
      ) : d(new PluginItemPrivate)
  {
    d->name = name;
    d->description = description;
    d->type = type;
    d->fileName = fileName;
    d->absoluteFilePath = filePath;
    d->enabled = enabled;
    d->factory = factory;
  }

  PluginItem::~PluginItem()
  {
    delete d;
  }

  int PluginItem::type() const
  {
    return d->type;
  }

  QString PluginItem::name() const
  {
    return d->name;
  }

  QString PluginItem::description() const
  {
    return d->description;
  }

  QString PluginItem::fileName() const
  {
    return d->fileName;
  }

  QString PluginItem::absoluteFilePath() const
  {
    return d->absoluteFilePath;
  }

  bool PluginItem::isEnabled() const
  {
    return d->enabled;
  }

  PluginFactory *PluginItem::factory() const
  {
    return d->factory;
  }

  void PluginItem::setType( Plugin::Type type )
  {
    d->type = type;
  }

  void PluginItem::setName( const QString &name )
  {
    d->name = name;
  }

  void PluginItem::setDescription( const QString &description )
  {
    d->description = description;
  }

  void PluginItem::setFileName( const QString &fileName )
  {
    d->fileName = fileName;
  }

  void PluginItem::setAbsoluteFilePath( const QString &filePath )
  {
    d->absoluteFilePath = filePath;
  }

  void PluginItem::setEnabled( bool enable )
  {
    d->enabled = enable;
  }

  void PluginItem::setFactory( PluginFactory *factory)
  {
    d->factory = factory;
  }

  class PluginManagerPrivate
  {
    public:
      PluginManagerPrivate() :
        toolsLoaded(false),
        extensionsLoaded(false),
        colorsLoaded(false) {}
      ~PluginManagerPrivate() {}

      bool toolsLoaded;
      QList<Tool *> tools;
      bool extensionsLoaded;
      QList<Extension *> extensions;
      bool colorsLoaded;
      QList<Color *> colors;

      PluginDialog *dialog;

      static bool factoriesLoaded;
      static QVector<QList<PluginItem *> > &m_items();
      static QVector<QList<PluginFactory *> > &m_enabledFactories();
      static QVector<QList<PluginFactory *> > &m_disabledFactories();

  };

  bool PluginManagerPrivate::factoriesLoaded = false;

  PluginManager::PluginManager(QObject *parent) : QObject(parent), d(new PluginManagerPrivate)
  {
    d->dialog = 0;
  }

  PluginManager::~PluginManager()
  {
    if(d->dialog) {
      d->dialog->deleteLater();
    }

    QSettings settings;
    writeSettings(settings);
    delete(d);
  }

  PluginManager* PluginManager::instance()
  {
    static PluginManager *obj = 0;

    if (!obj)
      obj = new PluginManager();

    return obj;
  }

  QList<Extension *> PluginManager::extensions(QObject *parent) const
  {
    loadFactories();
    if(d->extensionsLoaded)
      return d->extensions;

    foreach(PluginFactory *factory, factories(Plugin::ExtensionType)) {
      Extension *extension = static_cast<Extension *>(factory->createInstance(parent));
      d->extensions.append(extension);
    }

    d->extensionsLoaded = true;

    return d->extensions;
  }

  QList<Tool *> PluginManager::tools(QObject *parent) const
  {
    loadFactories();
    if(d->toolsLoaded)
      return d->tools;

    foreach(PluginFactory *factory, factories(Plugin::ToolType)) {
      Tool *tool = static_cast<Tool *>(factory->createInstance(parent));
      d->tools.append(tool);
    }

    d->toolsLoaded = true;
    return d->tools;
  }

  QList<Color *> PluginManager::colors(QObject *parent) const
  {
    loadFactories();
    if(d->colorsLoaded)
      return d->colors;

    foreach(PluginFactory *factory, factories(Plugin::ColorType))  {
      Color *color = static_cast<Color *>(factory->createInstance(parent));
      d->colors.append(color);
    }

    d->colorsLoaded = true;
    return d->colors;
  }

  PluginFactory * PluginManager::factory(const QString &name, Plugin::Type type)
  {
    loadFactories();
    if(type < Plugin::TypeCount) {
      foreach(PluginFactory *factory,
              PluginManagerPrivate::m_enabledFactories()[type]) {
        if(factory->name() == name) {
          return factory;
        }
      }
    }

    return 0;
  }

  Extension* PluginManager::extension(const QString &name, QObject *parent)
  {
    loadFactories();
    foreach(PluginFactory *factory, factories(Plugin::ExtensionType)) {
      if (factory->name() == name) {
        Extension *extension = static_cast<Extension *>(factory->createInstance(parent));
        return extension;
      }
    }

    return 0;
  }

  Tool* PluginManager::tool(const QString &name, QObject *parent)
  {
    loadFactories();
    foreach(PluginFactory *factory, factories(Plugin::ToolType)) {
      if (factory->name() == name) {
        Tool *tool = static_cast<Tool *>(factory->createInstance(parent));
        return tool;
      }
    }

    return 0;
  }

  Color* PluginManager::color(const QString &name, QObject *parent)
  {
    loadFactories();
    foreach(PluginFactory *factory, factories(Plugin::ColorType)) {
      if (factory->name() == name) {
        Color *color = static_cast<Color *>(factory->createInstance(parent));
        return color;
      }
    }

    return 0;
  }

  Engine* PluginManager::engine(const QString &name, QObject *parent)
  {
    loadFactories();

    foreach(PluginFactory *factory, factories(Plugin::EngineType)) {
      if (factory->name() == name) {
        Engine *engine = static_cast<Engine *>(factory->createInstance(parent));
        return engine;
      }
    }

    return 0;
  }

  QList<QString> PluginManager::names(Plugin::Type type)
  {
    loadFactories();

    QList<QString> names;
    foreach(PluginFactory *factory, factories(type))
      names.append(factory->name());

    return names;
  }

  QList<QString> PluginManager::descriptions(Plugin::Type type)
  {
    loadFactories();

    QList<QString> descriptions;
    foreach(PluginFactory *factory, factories(type))
      descriptions.append(factory->description());

    return descriptions;
  }

  QVector<QList<PluginItem *> > &PluginManagerPrivate::m_items()
  {
    static QVector<QList<PluginItem *> > items;

    if(items.size() < Plugin::TypeCount)
      items.resize(Plugin::TypeCount);

    return items;
  }

  QVector<QList<PluginFactory *> > &PluginManagerPrivate::m_enabledFactories()
  {
    static QVector<QList<PluginFactory *> > factories;

    if(factories.size() < Plugin::TypeCount)
      factories.resize(Plugin::TypeCount);

    return factories;
  }

  QVector<QList<PluginFactory *> > &PluginManagerPrivate::m_disabledFactories()
  {
    static QVector<QList<PluginFactory *> > factories;

    if(factories.size() < Plugin::TypeCount)
      factories.resize(Plugin::TypeCount);

    return factories;
  }

  void PluginManager::loadFactories()
  {
    if (PluginManagerPrivate::factoriesLoaded)
      return;

    QVector< QList<PluginFactory *> > &ef = PluginManagerPrivate::m_enabledFactories();

    // Load the static plugins first
    PluginFactory *bsFactory = qobject_cast<PluginFactory *>(new BSDYEngineFactory);
    if (bsFactory) {
      ef[bsFactory->type()].append(bsFactory);
    }
    else {
      qDebug() << "Instantiation of the static ball and sticks plugin failed.";
    }

    PluginFactory *elementFactory = qobject_cast<PluginFactory *>(new ElementColorFactory);
    if (elementFactory) {
      ef[elementFactory->type()].append(elementFactory);
    }
    else {
      qDebug() << "Instantiation of the static element color plugin failed.";
    }

    // Set up the paths
    QStringList pluginPaths;

    // Krazy: Use QProcess:
    // http://doc.trolltech.com/4.3/qprocess.html#systemEnvironment
    if (getenv("AVOGADRO_PLUGINS") != NULL) {
      pluginPaths << QString(getenv("AVOGADRO_PLUGINS")).split(':');
    }
    else {
      QString prefixPath = QString(INSTALL_PREFIX) + '/'
        + QString(INSTALL_LIBDIR) + "/avogadro";
      pluginPaths << prefixPath;

      #ifdef WIN32
        pluginPaths << QCoreApplication::applicationDirPath();
      #endif
    }

    // Load the plugins
    QSettings settings;
    settings.beginGroup("Plugins");
    foreach (const QString& path, pluginPaths) {
      loadPluginDir(path + "/colors", settings);
      loadPluginDir(path + "/engines", settings);
      loadPluginDir(path + "/exntesions", settings);
      loadPluginDir(path + "/tools", settings);
    }
    settings.endGroup();
    PluginManagerPrivate::factoriesLoaded = true;
  }

  QList<PluginFactory *> PluginManager::factories( Plugin::Type type )
  {
    if (type < PluginManagerPrivate::m_enabledFactories().size()) {
      loadFactories();
      return PluginManagerPrivate::m_enabledFactories()[type];
    }

    return QList<PluginFactory *>();
  }

  void PluginManager::showDialog()
  {
    if (!d->dialog) {
      d->dialog = new PluginDialog();
      connect(d->dialog, SIGNAL(reloadPlugins()), this, SLOT(reload()));
    }

    d->dialog->show();
  }

  void PluginManager::reload()
  {
    // make sure to write the settings before reloading
    QSettings settings;
    writeSettings(settings); // the isEnabled settings for all plugins

    // delete the dialog with the now invalid model
    if (d->dialog) {
      d->dialog->deleteLater();
      d->dialog = 0;
    }

    // write the tool settings
    settings.beginGroup("tools");
    foreach(Tool *tool, d->tools) {
      tool->writeSettings(settings);
      tool->deleteLater(); // and delete the tool, this will inform the
                           // ToolGroup which will inform the GLWidget.
    }
    settings.endGroup();

    // set toolsLoaded to false and clear the tools list
    d->toolsLoaded = false;
    d->tools.clear();

    // write the extension settings
    settings.beginGroup("extensions");
    foreach(Extension *extension, d->extensions) {
      extension->writeSettings(settings);
      extension->deleteLater(); // and delete the extension, when the QACtions
                                // are deleted, they are removed from the menu.
    }
    settings.endGroup();

    // set extensionsLoaded to false and clear the extensions list
    d->extensionsLoaded = false;
    d->extensions.clear();


    PluginManagerPrivate::factoriesLoaded = false;

    // delete the ProjectItem objects and clear the list
    for(int i=0; i<Plugin::TypeCount; i++) {
      foreach(PluginItem *item, PluginManagerPrivate::m_items()[i])
        delete item;
    }
    PluginManagerPrivate::m_items().clear();

    // delete the enabled PluginFactory objects and clear the list
    for(int i=0; i<Plugin::TypeCount; i++) {
      foreach(PluginFactory *factory, PluginManagerPrivate::m_enabledFactories()[i])
        delete factory;
    }
    PluginManagerPrivate::m_enabledFactories().clear();

    // delete the disabled PluginFactory objects and clear the list
    for(int i=0; i<Plugin::TypeCount; i++) {
      foreach(PluginFactory *factory, PluginManagerPrivate::m_disabledFactories()[i])
        delete factory;
    }
    PluginManagerPrivate::m_disabledFactories().clear();

    emit reloadPlugins();
  }


  QList<PluginItem *> PluginManager::pluginItems(Plugin::Type type)
  {
    return PluginManagerPrivate::m_items()[type];
  }

  void PluginManager::writeSettings(QSettings &settings)
  {
    // write the plugin item's isEnabled()
    settings.beginGroup("Plugins");
    for(int i=0; i<Plugin::TypeCount; i++) {
      settings.beginGroup(QString::number(i));
      foreach(PluginItem *item, PluginManagerPrivate::m_items()[i]) {
        settings.setValue(item->name(), item->isEnabled());
      }
      settings.endGroup();
    }
    settings.endGroup();
  }

  inline void PluginManager::loadPluginDir(const QString &directory,
                                           QSettings &settings)
  {
    QVector< QList<PluginFactory *> > &ef = PluginManagerPrivate::m_enabledFactories();
    QVector< QList<PluginFactory *> > &df = PluginManagerPrivate::m_disabledFactories();
    QDir dir(directory);
#ifdef Q_WS_X11
    QStringList dirFilters;
    dirFilters << "*.so";
    dir.setNameFilters(dirFilters);
    dir.setFilter(QDir::Files | QDir::Readable);
#endif
    qDebug() << "Searching for plugins in" << directory;
    foreach (const QString& fileName, dir.entryList(QDir::Files)) {
      QPluginLoader loader(dir.absoluteFilePath(fileName));
      QObject *instance = loader.instance();
      PluginFactory *factory = qobject_cast<PluginFactory *>(instance);
      if (factory) {
        settings.beginGroup(QString::number(factory->type()));
        PluginItem *item = new PluginItem(factory->name(), factory->description(), factory->type(), fileName, dir.absoluteFilePath(fileName), factory);
        if(settings.value(factory->name(), true).toBool()) {
          ef[factory->type()].append(factory);
          item->setEnabled(true);
        }
        else {
          df[factory->type()].append(factory);
          item->setEnabled(false);
        }
        PluginManagerPrivate::m_items()[factory->type()].append(item);
        settings.endGroup();
      }
      else {
        qDebug() << fileName << "failed to load. " << loader.errorString();
      }
    }
  }

}

#include "pluginmanager.moc"
