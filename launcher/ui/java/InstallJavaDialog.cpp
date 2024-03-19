// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2024 Trial97 <alexandru.tripon97@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "InstallJavaDialog.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "Application.h"
#include "FileSystem.h"
#include "java/download/ArchiveDownloadTask.h"
#include "java/download/ManifestDownloadTask.h"
#include "meta/Index.h"
#include "meta/VersionList.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/java/VersionList.h"
#include "ui/widgets/PageContainer.h"
#include "ui/widgets/VersionSelectWidget.h"

class InstallLoaderPage : public QWidget, public BasePage {
   public:
    Q_OBJECT
   public:
    explicit InstallLoaderPage(const QString& id, const QString& iconName, const QString& name, QWidget* parent = nullptr)
        : QWidget(parent), uid(id), iconName(iconName), name(name)
    {
        setObjectName(QStringLiteral("VersionSelectWidget"));
        horizontalLayout = new QHBoxLayout(this);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);

        majorVersionSelect = new VersionSelectWidget(this);
        majorVersionSelect->selectCurrent();
        majorVersionSelect->setEmptyString(tr("No java versions are currently available in the meta."));
        majorVersionSelect->setEmptyErrorString(tr("Couldn't load or download the java version lists!"));
        horizontalLayout->addWidget(majorVersionSelect, 1);

        javaVersionSelect = new VersionSelectWidget(this);
        javaVersionSelect->setEmptyString(tr("No java versions are currently available for your OS."));
        javaVersionSelect->setEmptyErrorString(tr("Couldn't load or download the java version lists!"));
        horizontalLayout->addWidget(javaVersionSelect, 4);
        connect(majorVersionSelect, &VersionSelectWidget::selectedVersionChanged, this, &InstallLoaderPage::setSelectedVersion);
        connect(javaVersionSelect, &VersionSelectWidget::selectedVersionChanged, this, &InstallLoaderPage::selectedVersionChanged);

        QMetaObject::connectSlotsByName(this);
    }
    ~InstallLoaderPage()
    {
        delete horizontalLayout;
        delete majorVersionSelect;
        delete javaVersionSelect;
    }

    //! loads the list if needed.
    void initialize(Meta::VersionList::Ptr vlist)
    {
        vlist->setProvidedRoles({ BaseVersionList::VersionRole, BaseVersionList::RecommendedRole, BaseVersionList::VersionPointerRole });
        majorVersionSelect->initialize(vlist.get());
    }

    void setSelectedVersion(BaseVersion::Ptr version)
    {
        auto dcast = std::dynamic_pointer_cast<Meta::Version>(version);
        if (!dcast) {
            return;
        }
        javaVersionSelect->initialize(new Java::VersionList(dcast, this));
        javaVersionSelect->selectCurrent();
    }

    QString id() const override { return uid; }
    QString displayName() const override { return name; }
    QIcon icon() const override { return APPLICATION->getThemedIcon(iconName); }

    void openedImpl() override
    {
        if (loaded)
            return;

        const auto versions = APPLICATION->metadataIndex()->get(uid);
        if (!versions)
            return;

        initialize(versions);
        loaded = true;
    }

    void setParentContainer(BasePageContainer* container) override
    {
        auto dialog = dynamic_cast<QDialog*>(dynamic_cast<PageContainer*>(container)->parent());
        connect(javaVersionSelect->view(), &QAbstractItemView::doubleClicked, dialog, &QDialog::accept);
    }

    BaseVersion::Ptr selectedVersion() const { return javaVersionSelect->selectedVersion(); }
    void selectSearch() { javaVersionSelect->selectSearch(); }
    void loadList()
    {
        majorVersionSelect->loadList();
        javaVersionSelect->loadList();
    }
   signals:
    void selectedVersionChanged(BaseVersion::Ptr version);

   private:
    const QString uid;
    const QString iconName;
    const QString name;
    bool loaded = false;

    QHBoxLayout* horizontalLayout = nullptr;
    VersionSelectWidget* majorVersionSelect = nullptr;
    VersionSelectWidget* javaVersionSelect = nullptr;
};

static InstallLoaderPage* pageCast(BasePage* page)
{
    auto result = dynamic_cast<InstallLoaderPage*>(page);
    Q_ASSERT(result != nullptr);
    return result;
}
namespace Java {

InstallDialog::InstallDialog(const QString& uid, QWidget* parent)
    : QDialog(parent), container(new PageContainer(this, QString(), this)), buttons(new QDialogButtonBox(this))
{
    auto layout = new QVBoxLayout(this);

    container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    layout->addWidget(container);

    auto buttonLayout = new QHBoxLayout(this);

    auto refreshButton = new QPushButton(tr("&Refresh"), this);
    connect(refreshButton, &QPushButton::clicked, this, [this] { pageCast(container->selectedPage())->loadList(); });
    buttonLayout->addWidget(refreshButton);

    buttons->setOrientation(Qt::Horizontal);
    buttons->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Download"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonLayout->addWidget(buttons);

    layout->addLayout(buttonLayout);

    setWindowTitle(dialogTitle());
    setWindowModality(Qt::WindowModal);
    resize(840, 480);

    for (BasePage* page : container->getPages()) {
        if (page->id() == uid)
            container->selectPage(page->id());

        connect(pageCast(page), &InstallLoaderPage::selectedVersionChanged, this, [this, page] {
            if (page->id() == container->selectedPage()->id())
                validate(container->selectedPage());
        });
    }
    connect(container, &PageContainer::selectedPageChanged, this, [this](BasePage* previous, BasePage* current) { validate(current); });
    pageCast(container->selectedPage())->selectSearch();
    validate(container->selectedPage());
}

QList<BasePage*> InstallDialog::getPages()
{
    return {
        // NeoForge
        new InstallLoaderPage("net.minecraft.java", "", tr("Mojang")),
        // Forge
        new InstallLoaderPage("net.adoptium.java", "", tr("Adoptium")),
        // Fabric
        new InstallLoaderPage("com.azul.java", "", tr("Azul")),
    };
}

QString InstallDialog::dialogTitle()
{
    return tr("Install Loader");
}

void InstallDialog::validate(BasePage* page)
{
    buttons->button(QDialogButtonBox::Ok)->setEnabled(pageCast(page)->selectedVersion() != nullptr);
}

void InstallDialog::done(int result)
{
    if (result == Accepted) {
        auto* page = pageCast(container->selectedPage());
        if (page->selectedVersion()) {
            auto meta = std::dynamic_pointer_cast<Java::Metadata>(page->selectedVersion());
            if (meta) {
                Task::Ptr task;
                auto final_path = FS::PathCombine(APPLICATION->javaPath(), meta->m_name);
                switch (meta->downloadType) {
                    case Java::DownloadType::Manifest:
                        task = makeShared<ManifestDownloadTask>(meta->url, final_path, meta->checksumType, meta->checksumHash);
                        break;
                    case Java::DownloadType::Archive:
                        task = makeShared<ArchiveDownloadTask>(meta->url, final_path, meta->checksumType, meta->checksumHash);
                        break;
                }
                auto deletePath = [final_path] { FS::deletePath(final_path); };
                connect(task.get(), &Task::failed, this, deletePath);
                connect(task.get(), &Task::aborted, this, deletePath);
                ProgressDialog pg(this);
                pg.setSkipButton(true, tr("Abort"));
                pg.execWithTask(task.get());
            }
        }
    }

    QDialog::done(result);
}
}  // namespace Java

#include "InstallJavaDialog.moc"