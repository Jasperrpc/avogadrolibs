/******************************************************************************

  This source file is part of the Avogadro project.

  Copyright 2014 Kitware, Inc.

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

******************************************************************************/

#include "playertool.h"
#include "gif.h"

#include <avogadro/core/vector.h>
#include <avogadro/qtgui/molecule.h>

#include <QtCore/QBuffer>
#include <QtCore/QProcess>
#include <QtGui/QIcon>
#include <QtGui/QOpenGLFramebufferObject>
#include <QtWidgets/QAction>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QOpenGLWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

#include <QDebug>

#include <cmath>

namespace Avogadro {
namespace QtPlugins {

using QtGui::Molecule;

PlayerTool::PlayerTool(QObject* parent_)
  : QtGui::ToolPlugin(parent_)
  , m_activateAction(new QAction(this))
  , m_molecule(nullptr)
  , m_renderer(nullptr)
  , m_currentFrame(0)
  , m_toolWidget(nullptr)
  , m_info(nullptr)
{
  m_activateAction->setText(tr("Player"));
  m_activateAction->setIcon(QIcon(":/icons/player.png"));
}

PlayerTool::~PlayerTool() {}

QWidget* PlayerTool::toolWidget() const
{
  if (!m_toolWidget) {
    m_toolWidget = new QWidget(qobject_cast<QWidget*>(parent()));
    QVBoxLayout* layout = new QVBoxLayout;
    QHBoxLayout* controls = new QHBoxLayout;
    controls->addStretch(1);
    QPushButton* button = new QPushButton("<");
    connect(button, SIGNAL(clicked()), SLOT(back()));
    controls->addWidget(button);
    button = new QPushButton(tr("Play"));
    connect(button, SIGNAL(clicked()), SLOT(play()));
    controls->addWidget(button);
    button = new QPushButton(tr("Stop"));
    connect(button, SIGNAL(clicked()), SLOT(stop()));
    controls->addWidget(button);
    button = new QPushButton(">");
    connect(button, SIGNAL(clicked()), SLOT(forward()));
    controls->addWidget(button);
    controls->addStretch(1);
    layout->addLayout(controls);

    QHBoxLayout* frames = new QHBoxLayout;
    QLabel* label = new QLabel(tr("Frame rate:"));
    frames->addWidget(label);
    m_animationFPS = new QSpinBox;
    m_animationFPS->setValue(5);
    m_animationFPS->setMinimum(0);
    m_animationFPS->setMaximum(100);
    m_animationFPS->setSuffix(tr(" FPS", "frames per second"));
    frames->addWidget(m_animationFPS);
    layout->addLayout(frames);

    QHBoxLayout* bonding = new QHBoxLayout;
    bonding->addStretch(1);
    m_dynamicBonding = new QCheckBox(tr("Dynamic bonding?"));
    m_dynamicBonding->setChecked(true);
    bonding->addWidget(m_dynamicBonding);
    bonding->addStretch(1);
    layout->addLayout(bonding);

    QHBoxLayout* recordLayout = new QHBoxLayout;
    recordLayout->addStretch(1);
    button = new QPushButton(tr("Record Movie..."));
    connect(button, SIGNAL(clicked()), SLOT(recordMovie()));
    recordLayout->addWidget(button);
    recordLayout->addStretch(1);
    layout->addLayout(recordLayout);

    m_info = new QLabel(tr("Stopped"));
    layout->addWidget(m_info);
    m_toolWidget->setLayout(layout);
  }
  connect(&m_timer, SIGNAL(timeout()), SLOT(animate()));

  return m_toolWidget;
}

QUndoCommand* PlayerTool::mousePressEvent(QMouseEvent*)
{
  return nullptr;
}

QUndoCommand* PlayerTool::mouseReleaseEvent(QMouseEvent*)
{
  return nullptr;
}

QUndoCommand* PlayerTool::mouseDoubleClickEvent(QMouseEvent*)
{
  return nullptr;
}

void PlayerTool::setActiveWidget(QWidget* widget)
{
  m_glWidget = qobject_cast<QOpenGLWidget*>(widget);
}

void PlayerTool::back()
{
  animate(-1);
}

void PlayerTool::forward()
{
  animate(1);
}

void PlayerTool::play()
{
  double fps = static_cast<double>(m_animationFPS->value());
  if (fps < 0.00001)
    fps = 5;
  int timeOut = static_cast<int>(1000 / fps);
  if (m_timer.isActive())
    m_timer.stop();
  m_timer.start(timeOut);
}

void PlayerTool::stop()
{
  m_timer.stop();
  m_info->setText(tr("Stopped"));
}

void PlayerTool::animate(int advance)
{
  if (m_molecule) {
    if (m_currentFrame < m_molecule->coordinate3dCount() - advance &&
        m_currentFrame + advance >= 0) {
      m_currentFrame += advance;
      m_molecule->setCoordinate3d(m_currentFrame);
    } else {
      m_currentFrame = advance > 0 ? 0 : m_molecule->coordinate3dCount() - 1;
      m_molecule->setCoordinate3d(m_currentFrame);
    }
    if (m_dynamicBonding->isChecked()) {
      m_molecule->clearBonds();
      m_molecule->perceiveBondsSimple();
    }
    m_molecule->emitChanged(Molecule::Atoms | Molecule::Added);
    m_info->setText(tr("Frame %0 of %1")
                      .arg(m_currentFrame + 1)
                      .arg(m_molecule->coordinate3dCount()));
  }
}

void PlayerTool::recordMovie()
{
  int EXPORT_WIDTH = 800, EXPORT_HEIGHT = 600;
  if (m_timer.isActive())
    m_timer.stop();

  QString baseFileName;
  if (m_molecule)
    baseFileName = m_molecule->data("fileName").toString().c_str();
  QFileInfo info(baseFileName);

  QString selfFilter = tr("Movie (*.mp4)");
  QString baseName = QFileDialog::getSaveFileName(
    qobject_cast<QWidget*>(parent()), tr("Export Bitmap Graphics"), "",
    tr("Movie (*.mp4);;GIF (*.gif)"), &selfFilter);

  if (baseName.isEmpty())
    return;
  if (!QFileInfo(baseName).suffix().isEmpty())
    baseName =
      QFileInfo(baseName).absolutePath() + "/" + QFileInfo(baseName).baseName();

  bool bonding = m_dynamicBonding->isChecked();
  int numberLength = static_cast<int>(
    ceil(log10(static_cast<float>(m_molecule->coordinate3dCount()) + 1)));
  m_glWidget->resize(EXPORT_WIDTH, EXPORT_HEIGHT);

  GifWriter writer;
  if (selfFilter == tr("GIF (*.gif)"))
    GifBegin(&writer, (baseName + ".gif").toLatin1().data(), EXPORT_WIDTH,
             EXPORT_HEIGHT, 100);

  for (int i = 0; i < m_molecule->coordinate3dCount(); ++i) {
    m_molecule->setCoordinate3d(i);
    if (bonding) {
      m_molecule->clearBonds();
      m_molecule->perceiveBondsSimple();
    }
    m_molecule->emitChanged(Molecule::Atoms | Molecule::Modified);
    QString fileName = QString::number(i);
    while (fileName.length() < numberLength)
      fileName.prepend('0');
    fileName.prepend(baseName);
    fileName.append(".png");

    QImage exportImage;
    m_glWidget->raise();
    m_glWidget->repaint();
    if (QOpenGLFramebufferObject::hasOpenGLFramebufferObjects()) {
      exportImage = m_glWidget->grabFramebuffer();
    } else {
      QPixmap pixmap = QPixmap::grabWindow(m_glWidget->winId());
      exportImage = pixmap.toImage();
    }

    if (selfFilter == tr("GIF (*.gif)")) {
      int frameWidth = exportImage.width();
      int frameHeight = exportImage.height();
      int numbPixels = frameWidth * frameHeight;

      uint8_t* imageData = new uint8_t[numbPixels * 4];
      int imageIndex = 0;
      for (int j = 0; j < frameHeight; ++j) {
        for (int k = 0; k < frameWidth; ++k) {
          QColor color = exportImage.pixel(k, j);
          imageData[imageIndex] = (uint8_t)color.red();
          imageData[imageIndex + 1] = (uint8_t)color.green();
          imageData[imageIndex + 2] = (uint8_t)color.blue();
          imageData[imageIndex + 3] = (uint8_t)color.alpha();
          imageIndex += 4;
        }
      }

      GifWriteFrame(&writer, imageData, EXPORT_WIDTH, EXPORT_HEIGHT, 10);
    } else if (selfFilter == tr("Movie (*.mp4)")) {
      if (!exportImage.save(fileName)) {
        QMessageBox::warning(qobject_cast<QWidget*>(parent()), tr("Avogadro"),
                             tr("Cannot save file %1.").arg(fileName));
        return;
      }
    }
  }

  if (selfFilter == tr("GIF (*.gif)")) {
    GifEnd(&writer);
  } else if (selfFilter == tr("Movie (*.mp4)")) {
    QProcess proc;
    QStringList args;
    args << "-y"
         << "-r" << QString::number(m_animationFPS->value()) << "-i"
         << baseName + "%0" + QString::number(numberLength) + "d.png"
         << "-c:v"
         << "libx264"
         << "-r"
         << "30"
         << "-pix_fmt"
         << "yuv420p" << baseName + ".mp4";
    proc.execute("avconv", args);
  }
}

} // namespace QtPlugins
} // namespace Avogadro
