#include "plot.h"

#include <QtDataVisualization/qvalue3daxis.h>
#include <QtDataVisualization/q3dscene.h>
#include <QtDataVisualization/q3dcamera.h>
#include <QtCore/qmath.h>

using namespace QtDataVisualization;

const int sampleCountX = 50;
const int sampleCountZ = 50;
const float sampleMin = -8.0f;
const float sampleMax = 8.0f;
const float kBallYOffset = 10.f;
const float kArrowOffset = 0.4;

Plot::Plot(Q3DSurface *surface)
    : gradient_descent(new VanillaGradientDescent),
      momemtum(new Momentum),
      ada_grad(new AdaGrad),
      rms_prop(new RMSProp),
      adam(new Adam),
      m_graph(surface),
      m_surfaceProxy(new QSurfaceDataProxy()),
      m_surfaceSeries(new QSurface3DSeries(m_surfaceProxy.get()))
{
    stepX = (sampleMax - sampleMin) / float(sampleCountX - 1);
    stepZ = (sampleMax - sampleMin) / float(sampleCountZ - 1);

    initializeGraph();

    all_descents.push_back(gradient_descent.get());
    all_descents.push_back(momemtum.get());
    all_descents.push_back(ada_grad.get());
    all_descents.push_back(rms_prop.get());
    all_descents.push_back(adam.get());

    for (auto& descent : all_descents) initializeBall(descent);
    initializeArrow(gradient_descent.get());

    initializeSurface();

    QObject::connect(&m_timer, &QTimer::timeout, this,
                     &Plot::triggerAnimation);

    // restart animation from selected position on mouse click
    QObject::connect(m_surfaceSeries.get(),
                     &QSurface3DSeries::selectedPointChanged,
                     this, &Plot::restartFromNewPosition);

    toggleAnimation();
}

Plot::~Plot() {}

void Plot::initializeGraph(){
    m_graph->setShadowQuality(QAbstract3DGraph::ShadowQualityNone);
    m_graph->activeTheme()->setType(Q3DTheme::Theme(2));
    m_graph->scene()->activeCamera()->setCameraPreset(Q3DCamera::CameraPresetFrontHigh);
    m_graph->setAxisX(new QValue3DAxis);
    m_graph->setAxisY(new QValue3DAxis);
    m_graph->setAxisZ(new QValue3DAxis);
}

void Plot::initializeBall(GradientDescent* descent){
    descent->ball->setScaling(QVector3D(0.01f, 0.01f, 0.01f));
    descent->ball->setMeshFile(QStringLiteral(":/mesh/largesphere.obj"));
    QImage pointColor = QImage(2, 2, QImage::Format_RGB32);
    pointColor.fill(descent->ball_color);
    descent->ball->setTextureImage(pointColor);
    m_graph->addCustomItem(descent->ball.get());

    restartAnimation();
}


void Plot::initializeArrow(GradientDescent* descent){
    descent->arrowX->setMeshFile(QStringLiteral(":/mesh/narrowarrow.obj"));
    QImage pointColor = QImage(2, 2, QImage::Format_RGB32);
    pointColor.fill(Qt::black);
    descent->arrowX->setTextureImage(pointColor);
    QQuaternion xRotation = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, 90.);
    descent->arrowX->setRotation(xRotation);
    m_graph->addCustomItem(descent->arrowX.get());
    descent->arrowX->setPosition(descent->ball->position());
    descent->arrowX->setScaling(QVector3D(0.1f, 0.3f, 0.1f));

    descent->arrowZ->setMeshFile(QStringLiteral(":/mesh/narrowarrow.obj"));
    descent->arrowZ->setTextureImage(pointColor);
    QQuaternion zRotation = QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, 90.);
    descent->arrowZ->setRotation(zRotation);
    m_graph->addCustomItem(descent->arrowZ.get());
    descent->arrowZ->setPosition(descent->ball->position());
    descent->arrowZ->setScaling(QVector3D(0.1f, 0.3f, 0.1f));
}


void Plot::initializeSurface() {
    QSurfaceDataArray *dataArray = new QSurfaceDataArray;
    dataArray->reserve(sampleCountZ);
    for (int i = 0 ; i < sampleCountZ ; i++) {
        QSurfaceDataRow *newRow = new QSurfaceDataRow(sampleCountX);
        // Keep values within range bounds, since just adding step can cause minor drift due
        // to the rounding errors.
        float z = qMin(sampleMax, (i * stepZ + sampleMin));
        int index = 0;
        for (int j = 0; j < sampleCountX; j++) {
            float x = qMin(sampleMax, (j * stepX + sampleMin));
            float y = gradient_descent->f(x, z);
            (*newRow)[index++].setPosition(QVector3D(x, y, z));
        }
        *dataArray << newRow;
    }

    m_surfaceProxy->resetArray(dataArray);

    // surface look
    m_surfaceSeries->setDrawMode(QSurface3DSeries::DrawSurfaceAndWireframe);
    m_surfaceSeries->setFlatShadingEnabled(false);
    m_surfaceSeries->setBaseColor( QColor( 100, 0, 0, 255 ));
    //gradient
    QLinearGradient gr;
    gr.setColorAt(1.0, Qt::darkGreen);
    gr.setColorAt(0.3, Qt::yellow);
    gr.setColorAt(0.1, Qt::red);
    gr.setColorAt(0.0, Qt::darkRed);
    m_surfaceSeries->setBaseGradient(gr);
    m_surfaceSeries->setColorStyle(Q3DTheme::ColorStyleRangeGradient);

    m_graph->addSeries(m_surfaceSeries.get());
}


void Plot::setBallPosition(QCustom3DItem* ball, Point p){
    const float cutoff = 15;
    float y = gradient_descent->f(p.x, p.z);
    // hack: if the graph has a hole that's too deep, we can't see the ball
    // hardcode to lift the ball up
    if (gradient_descent->f(p.x + stepX, p.z) - y > cutoff ||
        gradient_descent->f(p.x, p.z + stepZ) - y > cutoff){
        y = std::max(gradient_descent->f(p.x + stepX, p.z),
                gradient_descent->f(p.x, p.z + stepZ) - y) - cutoff - 10;
    }
    else{
        // to make the ball look like it's above the surface
        y += kBallYOffset;
    }
    ball->setPosition(QVector3D(p.x, y, p.z));
}


void Plot::setArrowGeometry(GradientDescent* descent, Point grad){
    // scale
    descent->arrowX->setScaling(QVector3D(0.1f, 0.1f * grad.x, 0.1f));
    descent->arrowZ->setScaling(QVector3D(0.1f, 0.1f * grad.z, 0.1f));
    // translate
    QVector3D ball_position = descent->ball->position();
    descent->arrowX->setPosition(
                QVector3D(ball_position.x() - grad.x * kArrowOffset,
                          ball_position.y(),
                          ball_position.z()));
    descent->arrowZ->setPosition(
                QVector3D(ball_position.x(),
                          ball_position.y(),
                          ball_position.z() - grad.z * kArrowOffset));
}


void Plot::triggerAnimation() {
    if (timer_counter == 0){
        for (auto& descent : all_descents){
            if (descent->isConverged()) continue;
            Point p;
            for (int i = 0; i < animation_speedup; i++)
                p = descent->gradientStep();
            setBallPosition(descent->ball.get(), p);
            Point grad(descent->gradX(), descent->gradZ());
            setArrowGeometry(descent, grad);
        }
    }
    timer_counter = (timer_counter + 1) % animation_slowdown;
}


void Plot::toggleAnimation() {
    m_timer.isActive() ? m_timer.stop() : m_timer.start(15);
}


void Plot::restartAnimation() {
    for (auto& descent : all_descents){
        descent->resetPosition();
        Point p = descent->getPosition();
        setBallPosition(descent->ball.get(), p);
        Point grad(descent->gradX(), descent->gradZ());
        setArrowGeometry(descent, grad);
    }
}


void Plot::restartFromNewPosition(QPoint q_pos){
    if (q_pos == QSurface3DSeries::invalidSelectionPosition())
        return;
    // convert the 2d Qt internal point for to the 3d point on the series
    QVector3D p = m_surfaceProxy->itemAt(q_pos)->position();
    for (auto descent : all_descents){
        descent->setStartingPosition(p.x(), p.z());
    }
    restartAnimation();
}


void Plot::setCameraZoom(float zoom){
    m_graph->scene()->activeCamera()->setZoomLevel(zoom);
}

void Plot::setAnimationSpeed(int index){
    animation_slowdown = 1;
    animation_speedup = 1;
    switch (index) {
        case 0: animation_slowdown = 10; break;
        case 1: animation_slowdown = 5; break;
        case 2: break;
        case 3: animation_speedup = 5; break;
        case 4: animation_speedup = 10; break;
    }
}
