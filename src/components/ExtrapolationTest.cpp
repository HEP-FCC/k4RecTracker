
#include "DetInterface/IGeoSvc.h"
#include "DetInterface/ITrackingGeoSvc.h"
#include "RecInterface/ITrackSeedingTool.h"

#include "GaudiKernel/IRndmGenSvc.h"


#include "ACTS/Detector/TrackingGeometry.hpp"
#include "ACTS/EventData/Measurement.hpp"
#include "ACTS/Examples/BuildGenericDetector.hpp"
#include "ACTS/Extrapolation/ExtrapolationCell.hpp"
#include "ACTS/Extrapolation/ExtrapolationEngine.hpp"
#include "ACTS/Extrapolation/IExtrapolationEngine.hpp"
#include "ACTS/Extrapolation/MaterialEffectsEngine.hpp"
#include "ACTS/Extrapolation/RungeKuttaEngine.hpp"
#include "ACTS/Extrapolation/StaticEngine.hpp"
#include "ACTS/Extrapolation/StaticNavigationEngine.hpp"
#include "ACTS/Fitter/KalmanFitter.hpp"
#include "ACTS/Fitter/KalmanUpdator.hpp"
#include "ACTS/MagneticField/ConstantBField.hpp"
#include "ACTS/Surfaces/PerigeeSurface.hpp"
#include "ACTS/Utilities/Definitions.hpp"
#include "ACTS/Utilities/Identifier.hpp"
#include "ACTS/Utilities/Logger.hpp"

#include "datamodel/PositionedTrackHitCollection.h"
#include "datamodel/TrackHitCollection.h"

#include "DD4hep/LCDD.h"
#include "DD4hep/Volumes.h"
#include "DDRec/API/IDDecoder.h"
#include "DDSegmentation/BitField64.h"

#include <cmath>
#include <random>

#include "ExtrapolationTest.h"

using namespace Acts;

DECLARE_ALGORITHM_FACTORY(ExtrapolationTest)

ExtrapolationTest::ExtrapolationTest(const std::string& name, ISvcLocator* svcLoc) : GaudiAlgorithm(name, svcLoc) {

  declareProperty("positionedTrackHits", m_positionedTrackHits, "hits/TrackerPositionedHits");
}

ExtrapolationTest::~ExtrapolationTest() {}

StatusCode ExtrapolationTest::initialize() {

  IRndmGenSvc* randSvc = svc<IRndmGenSvc>("RndmGenSvc", true);

  m_geoSvc = service("GeoSvc");

  StatusCode sc = GaudiAlgorithm::initialize();
  if (sc.isFailure()) return sc;

  m_trkGeoSvc = service("TrackingGeoSvc");
  if (nullptr == m_trkGeoSvc) {
    error() << "Unable to locate Tracking Geometry Service. " << endmsg;
    return StatusCode::FAILURE;
  }

  m_trkGeo = m_trkGeoSvc->trackingGeometry();
  auto propConfig = RungeKuttaEngine<>::Config();
  propConfig.fieldService = std::make_shared<ConstantBField>(0, 0, m_magneticFieldBz * 0.001); // needs to be in kT
  auto propEngine = std::make_shared<RungeKuttaEngine<>>(propConfig);

  auto matConfig = MaterialEffectsEngine::Config();
  auto materialEngine = std::make_shared<MaterialEffectsEngine>(matConfig);

  auto navConfig = StaticNavigationEngine::Config();
  navConfig.propagationEngine = propEngine;
  navConfig.materialEffectsEngine = materialEngine;
  navConfig.trackingGeometry = m_trkGeo;
  auto navEngine = std::make_shared<StaticNavigationEngine>(navConfig);

  auto statConfig = StaticEngine::Config();
  statConfig.propagationEngine = propEngine;
  statConfig.navigationEngine = navEngine;
  statConfig.materialEffectsEngine = materialEngine;
  auto statEngine = std::make_shared<StaticEngine>(statConfig);

  auto exEngineConfig = ExtrapolationEngine::Config();
  exEngineConfig.trackingGeometry = m_trkGeo;
  exEngineConfig.propagationEngine = propEngine;
  exEngineConfig.navigationEngine = navEngine;
  exEngineConfig.extrapolationEngines = {statEngine};
  m_exEngine = std::make_shared<ExtrapolationEngine>(exEngineConfig);


  sc = m_flatDist.initialize(randSvc, Rndm::Flat(0., 1.));
  return sc;
}

StatusCode ExtrapolationTest::execute() {

  fcc::PositionedTrackHitCollection* phitscoll = new fcc::PositionedTrackHitCollection();
  fcc::TrackHitCollection* hitscoll = new fcc::TrackHitCollection();


  ActsVector<ParValue_t, NGlobalPars> pars;
  pars << 0, 0, m_flatDist() * M_PI * 0.5, m_flatDist() * M_PI*0.45, 0.001;
  auto startCov =
      std::make_unique<ActsSymMatrix<ParValue_t, NGlobalPars>>(ActsSymMatrix<ParValue_t, NGlobalPars>::Identity());

  const Surface* pSurf = m_trkGeo->getBeamline();
  auto startTP = std::make_unique<BoundParameters>(std::move(startCov), std::move(pars), *pSurf);

  ExtrapolationCell<TrackParameters> exCell(*startTP);
  exCell.addConfigurationMode(ExtrapolationMode::CollectSensitive);
  exCell.addConfigurationMode(ExtrapolationMode::CollectPassive);
  exCell.addConfigurationMode(ExtrapolationMode::CollectBoundary);


  debug() << "start extrapolation ..." << endmsg;
  m_exEngine->extrapolate(exCell);
  debug() << "got " << exCell.extrapolationSteps.size() << " extrapolation steps" << endmsg;

  for (const auto& step : exCell.extrapolationSteps) {
    const auto& tp = step.parameters;
    fcc::TrackHit edmHit = hitscoll->create();
    fcc::BareHit& edmHitCore = edmHit.core();
    auto position = fcc::Point();
    position.x = tp->position().x();
    position.y = tp->position().y();
    position.z = tp->position().z();
    phitscoll->create(position, edmHitCore);
  }

  m_positionedTrackHits.put(phitscoll);
  return StatusCode::SUCCESS;
}

StatusCode ExtrapolationTest::finalize() {
  StatusCode sc = GaudiAlgorithm::finalize();
  return sc;
}
