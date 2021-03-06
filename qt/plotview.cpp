#include <iostream>
#include <cassert>
#include <algorithm>

#include <QWheelEvent>
#include <QGraphicsItemGroup>

#include "plotview.h"
#include "signal.h"



struct PlotView::SignalWithInfo {
  QString                 signalName;
  std::unique_ptr<Signal> signal;
  QGraphicsItemGroup*     itemGroup;
  int                     position;
  int                     height;

  SignalWithInfo(QString signalName,
                 Signal* signal) : 
    signalName(signalName),
    signal(signal),
    itemGroup(0)
  {
    // nothing
  }

  ~SignalWithInfo()
  {
    delete itemGroup;
  }
};

PlotView::PlotView(QWidget *parent) :
  QGraphicsView(parent), m_grid(new QGraphicsItemGroup), m_signalUs(0),
  m_zoomNumerator(1), m_zoomDenominator(1),
  m_timeOfInterest(0), m_interestIndicator(0)
{
  m_scene = new QGraphicsScene(this);
  setScene(m_scene);

  m_scene->addItem(m_grid);

  connect(this, SIGNAL(signalUpdated()), this, SLOT(redraw()));
}

PlotView::~PlotView()
{
  // nothing
}

Signal*
PlotView::getSignalByIndex(int a_index)
{
  if (a_index < m_signals.size()) {
    return m_signals[a_index]->signal.get();
  } else {
    return 0;
  }
}



Signal*
PlotView::getSignalByName(QString a_name)
{
  auto it = std::find_if(m_signals.begin(), m_signals.end(), [&a_name](SignalWithInfo* x){ return x->signalName == a_name; });
  if (it != m_signals.end()) {
    return (*it)->signal.get();
  } else {
    return 0;
  }
}

void PlotView::removeSignal(QString a_name)
{
  auto it = std::find_if(m_signals.begin(), m_signals.end(), [&a_name](SignalWithInfo* x){ return x->signalName == a_name; });
  if (it != m_signals.end()) {
    delete *it;
    m_signals.erase(it);
  }
  emit signalUpdated();
}

void PlotView::setSignal(QString a_name, Signal* a_signal)
{
  removeSignal(a_name);
  m_signals.push_back(new SignalWithInfo(a_name, a_signal));
  emit signalUpdated();
}

void PlotView::mark(QString a_label, bool a_state, unsigned a_min, unsigned a_max, QColor color)
{
  ConditionCheckFactory* factory = new StateLengthCheckFactory(a_state, a_min, a_max);
  m_conditions[factory] = color;
  emit signalUpdated();
}

void PlotView::drawSignal(SignalWithInfo* si)
{
  const Signal& signal = *si->signal.get();
  bool     state    = true;
  unsigned prevTime = 0;

  if (si->itemGroup) {
    std::cerr << "Destroying group" << std::endl;
    delete si->itemGroup;
  }
  si->itemGroup = new QGraphicsItemGroup();
  m_scene->addItem(si->itemGroup);

  std::map<std::unique_ptr<ConditionSession>, ConditionMarker*> condSessions;
  for (auto& cond: m_conditions) {
    ConditionSession* session = cond.first->start();
    condSessions[std::unique_ptr<ConditionSession>(session)] = &cond.second;
  }

  auto addLine = [&](unsigned t0, unsigned t1, bool y0, bool y1, int width, QColor color, int yOfs) {
    QGraphicsLineItem* line =
    new QGraphicsLineItem(horizPosAt(t0), si->position + y0 * mc_heightScale + yOfs, 
                          horizPosAt(t1), si->position + y1 * mc_heightScale + yOfs,
                          si->itemGroup);
    QPen p = line->pen();
    p.setWidth(width);
    p.setColor(color);
    line->setPen(p);
  };

  for (auto curTime: signal.tds()) {
    std::list<QColor> colors;
    for (auto& cond: condSessions) {
      if ((*cond.first)(curTime, state)) {
        colors.push_back(*cond.second);
      }
    }
    if (colors.begin() != colors.end()) {
      int ofs = 0;
      for (auto& color: colors) {
        addLine(prevTime, curTime, !state, !state, 5, color, ofs);
        ofs += 3;
      }
    } else {
      addLine(prevTime, curTime, !state, !state, 2, QColor(Qt::black), 0);
    }
    addLine(curTime, curTime, 0, 1, 2, QColor(Qt::black), 0);
    prevTime = curTime;
    state = !state;
  }
}

void PlotView::redraw()
{
  redrawScene();
  redrawGrid();
  redrawPointOfInterest();
}

unsigned PlotView::horizPosAt(TDS time)
{
  return time * mc_widthScale * m_zoomNumerator / m_zoomDenominator;
}

unsigned PlotView::vertBegin(unsigned index)
{
  return index * (mc_heightScale + 10) + 10;
}

unsigned PlotView::vertEnd(unsigned index)
{
  return (index + 1) * (mc_heightScale + 10);
}

void PlotView::wheelEvent(QWheelEvent* event)
{
  if (event->delta() > 0) {
    zoomIn();
  } else if (event->delta() < 0) {
    zoomOut();
  }
}

void PlotView::mousePressEvent(QMouseEvent* a_event)
{
  QPointF at = mapToScene(a_event->x(), a_event->y());
  int signalIdx;
  TDS signalTime;

  if (getSignalPosition(at, signalIdx, signalTime)) {
    std::cerr << signalIdx << " , " << signalTime << std::endl;
    setPointOfInterest(signalTime);
  }
}

void PlotView::redrawPointOfInterest()
{
  delete m_interestIndicator;
  m_interestIndicator = new QGraphicsItemGroup();
  m_scene->addItem(m_interestIndicator);

  int signalIdx = 0;
  for (auto& si: m_signals) {
    Signal& signal = *si->signal;
    Signal::tds_iterator at = signal.findTime(m_timeOfInterest);
    if (at != signal.tds_end()) { 
      Signal::tds_iterator next = at;
      ++next;
      if (next != signal.tds_end()) {
        QGraphicsLineItem* line =
          new QGraphicsLineItem(horizPosAt(*at), si->position + si->height / 2,
                                horizPosAt(*next), si->position + si->height / 2,
                                m_interestIndicator);
        QPen p = line->pen();
        p.setWidth(6);
        p.setColor(QColor(Qt::blue));
        line->setPen(p);
      }
    }
    ++signalIdx;
  }
}

void PlotView::setPointOfInterest(TDS a_at)
{
  m_timeOfInterest = a_at;
  redrawPointOfInterest();
}

bool PlotView::getSignalPosition(QPointF a_at, int& a_signalIdx, TDS& a_signalTime)
{
  for (int signalIdx = 0; signalIdx < m_signals.size(); ++signalIdx) {
    if (a_at.y() >= vertBegin(signalIdx) &&
        a_at.y() <= vertEnd(signalIdx)) {
      a_signalIdx = signalIdx;
      a_signalTime = a_at.x() * m_zoomDenominator / m_zoomNumerator / mc_widthScale;
      return true;
    }
  }
  return false;
}

void PlotView::zoomIn()
{
  m_zoomNumerator *= 2;
  redraw();
}

void PlotView::zoomOut()
{
  m_zoomDenominator *= 2;
  redraw();
}

void PlotView::redrawGrid()
{
  delete m_grid;
  m_grid = new QGraphicsItemGroup();
  m_scene->addItem(m_grid);
  m_grid->setZValue(-10);

  QRectF sceneRect = m_scene->sceneRect();

  for (TDS t = 0; t < m_signalUs; t += 1000) {
    QGraphicsLineItem* line =
      new QGraphicsLineItem(horizPosAt(t), 0,
                            horizPosAt(t), sceneRect.height(),
                            m_grid);
    QPen p = line->pen();
    p.setColor(QColor(Qt::gray));
    line->setPen(p);
  }
}

void PlotView::redrawScene()
{
  if (m_signals.begin() != m_signals.end()) {
    m_signalUs = *(m_signals[0]->signal->tds_end() - 1);

    int width = horizPosAt(m_signalUs);
    int height = (mc_heightScale + 10) * m_signals.size() + 10;

    // QGraphicsScene* newScene = new QGraphicsScene(0, 0, width, height, this);
    // QGraphicsScene* oldScene = m_scene;
    // m_scene = newScene;
    m_scene->setSceneRect(0, 0, width, height);

    int index = 0;
    for (auto si: m_signals) {
      si->position = vertBegin(index);
      si->height = mc_heightScale;
      drawSignal(si);
      ++index;
    }

    // setScene(newScene);
    // delete oldScene;

    setMinimumSize(300, height + 30);
    setSceneRect(0, 0, width, height);
  } else {
    // hmm..
  }
}
