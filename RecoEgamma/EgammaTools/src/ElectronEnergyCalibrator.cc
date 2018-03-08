#include "RecoEgamma/EgammaTools/interface/ElectronEnergyCalibrator.h"

#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/Utilities/interface/RandomNumberGenerator.h"
#include "FWCore/Utilities/interface/Exception.h"
#include <CLHEP/Random/RandGaussQ.h>
#include "RecoEgamma/EgammaTools/interface/EGEnergySysIndex.h"

const EnergyScaleCorrection::ScaleCorrection ElectronEnergyCalibrator::defaultScaleCorr_;
const EnergyScaleCorrection::SmearCorrection ElectronEnergyCalibrator::defaultSmearCorr_;

ElectronEnergyCalibrator::ElectronEnergyCalibrator(const EpCombinationTool &combinator,
						   const std::string& correctionFile) :
  correctionRetriever_(correctionFile), 
  epCombinationTool_(&combinator), 
  rng_(nullptr),
  minEt_(1.0)
{
  
}

void ElectronEnergyCalibrator::initPrivateRng(TRandom *rnd)
{
  rng_ = rnd;
}

std::vector<float> ElectronEnergyCalibrator::
calibrate(reco::GsfElectron &ele,
	  const unsigned int runNumber, 
	  const EcalRecHitCollection *recHits, 
	  edm::StreamID const & id, 
	  const ElectronEnergyCalibrator::EventType eventType) const
{
  return calibrate(ele,runNumber,recHits,gauss(id),eventType);
}

std::vector<float> ElectronEnergyCalibrator::
calibrate(reco::GsfElectron &ele, unsigned int runNumber, 
	  const EcalRecHitCollection *recHits, 
	  const float smearNrSigma, 
	  const ElectronEnergyCalibrator::EventType eventType) const
{
  const float scEtaAbs = std::abs(ele.superCluster()->eta());
  const float et = ele.ecalEnergy() / cosh(scEtaAbs);

  if (et < minEt_) {
    std::vector<float> retVal(EGEnergySysIndex::kNrSysErrs,ele.energy());
    retVal[EGEnergySysIndex::kScaleValue]  = 1.0;
    retVal[EGEnergySysIndex::kSmearValue]  = 0.0;
    retVal[EGEnergySysIndex::kSmearNrSigma] = smearNrSigma;
    retVal[EGEnergySysIndex::kEcalPreCorr] = ele.ecalEnergy();
    retVal[EGEnergySysIndex::kEcalErrPreCorr] = ele.ecalEnergyError();
    retVal[EGEnergySysIndex::kEcalPostCorr] = ele.ecalEnergy();
    retVal[EGEnergySysIndex::kEcalErrPostCorr] = ele.ecalEnergyError();
    retVal[EGEnergySysIndex::kEcalTrkPreCorr] = ele.energy();
    retVal[EGEnergySysIndex::kEcalTrkErrPreCorr] = ele.corrections().combinedP4Error;
    retVal[EGEnergySysIndex::kEcalTrkPostCorr] = ele.energy();
    retVal[EGEnergySysIndex::kEcalTrkErrPostCorr] = ele.corrections().combinedP4Error;
    return retVal;
  }

  const DetId seedDetId = ele.superCluster()->seed()->seed();
  EcalRecHitCollection::const_iterator seedRecHit = recHits->find(seedDetId);
  unsigned int gainSeedSC = 12;
  if (seedRecHit != recHits->end()) { 
    if(seedRecHit->checkFlag(EcalRecHit::kHasSwitchToGain6)) gainSeedSC = 6;
    if(seedRecHit->checkFlag(EcalRecHit::kHasSwitchToGain1)) gainSeedSC = 1;
  }

  const EnergyScaleCorrection::ScaleCorrection* scaleCorr = correctionRetriever_.getScaleCorr(runNumber, et, scEtaAbs, ele.full5x5_r9(), gainSeedSC);  
  const EnergyScaleCorrection::SmearCorrection* smearCorr = correctionRetriever_.getSmearCorr(runNumber, et, scEtaAbs, ele.full5x5_r9(), gainSeedSC);  
  if(scaleCorr==nullptr) scaleCorr=&defaultScaleCorr_;
  if(smearCorr==nullptr) smearCorr=&defaultSmearCorr_;
  
  std::vector<float> uncertainties(EGEnergySysIndex::kNrSysErrs,0.);
  
  uncertainties[EGEnergySysIndex::kScaleValue]  = scaleCorr->scale();
  uncertainties[EGEnergySysIndex::kSmearValue]  = smearCorr->sigma(et); //even though we use scale = 1.0, we still store the value returned for MC
  uncertainties[EGEnergySysIndex::kSmearNrSigma]  = smearNrSigma;
  //MC central values are not scaled (scale = 1.0), data is not smeared (smearNrSigma = 0)
  //smearing still has a second order effect on data as it enters the E/p combination as an 
  //extra uncertainty on the calo energy
  //MC gets all the scale systematics
  if(eventType == EventType::DATA){ 
    setEnergyAndSystVarations(scaleCorr->scale(),0.,et,*scaleCorr,*smearCorr,ele,uncertainties);
  }else if(eventType == EventType::MC){
    setEnergyAndSystVarations(1.0,smearNrSigma,et,*scaleCorr,*smearCorr,ele,uncertainties);
  }
 
  return uncertainties;
  
}

void ElectronEnergyCalibrator::
setEnergyAndSystVarations(const float scale,const float smearNrSigma,const float et,
			  const EnergyScaleCorrection::ScaleCorrection& scaleCorr,
			  const EnergyScaleCorrection::SmearCorrection& smearCorr,
			  reco::GsfElectron& ele,
			  std::vector<float>& energyData)const
{
 
  const float smear = smearCorr.sigma(et);   
  const float smearRhoUp = smearCorr.sigma(et,1,0);
  const float smearRhoDn = smearCorr.sigma(et,-1,0);
  const float smearPhiUp = smearCorr.sigma(et,0,1);
  const float smearPhiDn = smearCorr.sigma(et,0,-1);
  const float smearUp = smearPhiUp;
  const float smearDn = smearPhiDn;

  const float corr = scale + smear * smearNrSigma;
  const float corrRhoUp = scale + smearRhoUp * smearNrSigma;
  const float corrRhoDn = scale + smearRhoDn * smearNrSigma;
  const float corrPhiUp = scale + smearPhiUp * smearNrSigma;
  const float corrPhiDn = scale + smearPhiDn * smearNrSigma;
  const float corrUp = corrPhiUp;
  const float corrDn = corrPhiDn;

  const float corrScaleStatUp = corr+scaleCorr.scaleErrStat();
  const float corrScaleStatDn = corr-scaleCorr.scaleErrStat();
  const float corrScaleSystUp = corr+scaleCorr.scaleErrSyst();
  const float corrScaleSystDn = corr-scaleCorr.scaleErrSyst();
  const float corrScaleGainUp = corr+scaleCorr.scaleErrGain();
  const float corrScaleGainDn = corr-scaleCorr.scaleErrGain();
  const float corrScaleUp = corr+scaleCorr.scaleErr(EnergyScaleCorrection::kErrStatSystGain);
  const float corrScaleDn = corr-scaleCorr.scaleErr(EnergyScaleCorrection::kErrStatSystGain);
  
  const math::XYZTLorentzVector oldP4 = ele.p4();
  energyData[EGEnergySysIndex::kEcalTrkPreCorr] = ele.energy();
  energyData[EGEnergySysIndex::kEcalTrkErrPreCorr] = ele.corrections().combinedP4Error;
  energyData[EGEnergySysIndex::kEcalPreCorr] = ele.ecalEnergy();
  energyData[EGEnergySysIndex::kEcalErrPreCorr] = ele.ecalEnergyError();
  
  energyData[EGEnergySysIndex::kScaleStatUp]   = calCombinedMom(ele,corrScaleStatUp,smear).first;
  energyData[EGEnergySysIndex::kScaleStatDown] = calCombinedMom(ele,corrScaleStatDn,smear).first;
  energyData[EGEnergySysIndex::kScaleSystUp]   = calCombinedMom(ele,corrScaleSystUp,smear).first;
  energyData[EGEnergySysIndex::kScaleSystDown] = calCombinedMom(ele,corrScaleSystDn,smear).first;
  energyData[EGEnergySysIndex::kScaleGainUp]   = calCombinedMom(ele,corrScaleGainUp,smear).first;
  energyData[EGEnergySysIndex::kScaleGainDown] = calCombinedMom(ele,corrScaleGainDn,smear).first;
  
  energyData[EGEnergySysIndex::kSmearRhoUp]   = calCombinedMom(ele,corrRhoUp,smearRhoUp).first;
  energyData[EGEnergySysIndex::kSmearRhoDown] = calCombinedMom(ele,corrRhoDn,smearRhoDn).first;
  energyData[EGEnergySysIndex::kSmearPhiUp]   = calCombinedMom(ele,corrPhiUp,smearPhiUp).first;
  energyData[EGEnergySysIndex::kSmearPhiDown] = calCombinedMom(ele,corrPhiDn,smearPhiDn).first;
  
  energyData[EGEnergySysIndex::kScaleUp]   = calCombinedMom(ele,corrScaleUp,smear).first;
  energyData[EGEnergySysIndex::kScaleDown] = calCombinedMom(ele,corrScaleDn,smear).first;
  energyData[EGEnergySysIndex::kSmearUp]   = calCombinedMom(ele,corrUp,smearUp).first;
  energyData[EGEnergySysIndex::kSmearDown] = calCombinedMom(ele,corrDn,smearDn).first;
  
  setEcalEnergy(ele,corr,smear);
  const std::pair<float, float> combinedMomentum = epCombinationTool_->combine(ele);
  const float energyCorr =  combinedMomentum.first / oldP4.t();

  const math::XYZTLorentzVector newP4(oldP4.x() * energyCorr,
				      oldP4.y() * energyCorr,
				      oldP4.z() * energyCorr,
				      combinedMomentum.first);
  
  ele.correctMomentum(newP4, ele.trackMomentumError(), combinedMomentum.second); 
  energyData[EGEnergySysIndex::kEcalTrkPostCorr] = ele.energy();
  energyData[EGEnergySysIndex::kEcalTrkErrPostCorr] = ele.corrections().combinedP4Error;
  
  energyData[EGEnergySysIndex::kEcalPostCorr] = ele.ecalEnergy();
  energyData[EGEnergySysIndex::kEcalErrPostCorr] = ele.ecalEnergyError();
  
}


void ElectronEnergyCalibrator::setEcalEnergy(reco::GsfElectron& ele,
					     const float scale,
					     const float smear)const
{
  const float oldEcalEnergy = ele.ecalEnergy();
  const float oldEcalEnergyErr = ele.ecalEnergyError();
  ele.setCorrectedEcalEnergy( oldEcalEnergy*scale );
  ele.setCorrectedEcalEnergyError(std::hypot( oldEcalEnergyErr*scale, oldEcalEnergy*smear*scale ) );
}

std::pair<float,float> ElectronEnergyCalibrator::calCombinedMom(reco::GsfElectron& ele,
								const float scale,
								const float smear)const
{ 
  const float oldEcalEnergy = ele.ecalEnergy();
  const float oldEcalEnergyErr = ele.ecalEnergyError();
  setEcalEnergy(ele,scale,smear);
  const auto& combinedMomentum = epCombinationTool_->combine(ele);
  ele.setCorrectedEcalEnergy(oldEcalEnergy);
  ele.setCorrectedEcalEnergyError(oldEcalEnergyErr);
  return combinedMomentum;
}
						      

double ElectronEnergyCalibrator::gauss(edm::StreamID const& id) const
{
  if (rng_) {
    return rng_->Gaus();
  } else {
    edm::Service<edm::RandomNumberGenerator> rng;
    if ( !rng.isAvailable() ) {
      throw cms::Exception("Configuration")
	<< "XXXXXXX requires the RandomNumberGeneratorService\n"
	"which is not present in the configuration file.  You must add the service\n"
	"in the configuration file or remove the modules that require it.";
    }
    CLHEP::RandGaussQ gaussDistribution(rng->getEngine(id), 0.0, 1.0);
    return gaussDistribution.fire();
  }
}
