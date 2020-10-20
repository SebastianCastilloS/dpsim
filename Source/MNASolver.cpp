/* Copyright 2017-2020 Institute for Automation of Complex Power Systems,
 *                     EONERC, RWTH Aachen University
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *********************************************************************************/


#include <dpsim/MNASolver.h>
#include <dpsim/SequentialScheduler.h>
#include <cps/DP/DP_Ph1_Transformer.h>
#include <cps/DP/DP_Ph1_AvVoltageSourceInverterDQ.h>
#include <cps/DP/DP_Ph1_RXLoad.h>

using namespace DPsim;
using namespace CPS;

namespace DPsim {

template <typename VarType>
MnaSolver<VarType>::MnaSolver(String name,
	CPS::Domain domain, CPS::Logger::Level logLevel) :
	Solver(name, logLevel), mDomain(domain) {

	// Raw source and solution vector logging
	mLeftVectorLog = std::make_shared<DataLogger>(name + "_LeftVector", logLevel != CPS::Logger::Level::off);
	mRightVectorLog = std::make_shared<DataLogger>(name + "_RightVector", logLevel != CPS::Logger::Level::off);
	mSysMatrixLog = std::make_shared<DataLogger>(name + "_SystemMatrix", logLevel != CPS::Logger::Level::off);
}

template <typename VarType>
void MnaSolver<VarType>::setSystem(CPS::SystemTopology system) {
	mSystem = system;
}

template <typename VarType>
void MnaSolver<VarType>::initialize() {
	mSLog->info("---- Start initialization ----");
	mSLog->info("-- Process system components");
	for (auto comp : mSystem.mComponents)
		mSLog->info("Added {:s} '{:s}' to simulation.", comp->type(), comp->name());

	// Otherwise LU decomposition will fail
	if (mSystem.mComponents.size() == 0)
		throw SolverException();

	// We need to differentiate between power and signal components and
	// ground nodes should be ignored.
	identifyTopologyObjects();
	// These steps complete the network information.
	createVirtualNodes();
	assignMatrixNodeIndices();

	mSLog->info("-- Create empty MNA system matrices and vectors");
	// The system topology is prepared and we create the MNA matrices.
	createEmptyVectors();
	

	// Register attribute for solution vector
	if (mFrequencyParallel) {
		mSLog->info("Computing network harmonics in parallel.");
		for(Int freq = 0; freq < mSystem.mFrequencies.size(); freq++) {
			addAttribute<Matrix>("left_vector_"+std::to_string(freq), mLeftSideVectorHarm.data()+freq, Flags::read);
			mLeftVectorHarmAttributes.push_back(attribute<Matrix>("left_vector_"+std::to_string(freq)));
		}
	}
	else {
		addAttribute<Matrix>("left_vector", &mLeftSideVector, Flags::read);
	}

	// Initialize components from powerflow solution and
	// calculate MNA specific initialization values
	initializeComponents();

	// NEW add also subswitches of elements
	for (auto comp : mMNAComponents) {
		// if it is VSI
		// TODO generalize this for all elements with switches
		auto swcomp = std::dynamic_pointer_cast<DP::Ph1::AvVoltageSourceInverterDQ>(comp);
		if (swcomp) {
			mVarElemsSwitches.push_back(swcomp->getProtectionSwitch());
		}
		//*/
		auto loadswitch = std::dynamic_pointer_cast<DP::Ph1::RXLoad>(comp);
		if (loadswitch) {
			mVarElemsSwitches.push_back(loadswitch->getProtectionSwitch());
		}
	}

	// NEW: create after mSwitches is updated
	createEmptySystemMatrix();

	if (mSteadyStateInit)
		steadyStateInitialization();

	// Some components feature a different behaviour for simulation and initialization
	for (auto comp : mSystem.mComponents) {
		auto powerComp = std::dynamic_pointer_cast<CPS::TopologicalPowerComp>(comp);
		if (powerComp) powerComp->setBehaviour(TopologicalPowerComp::Behaviour::Simulation);

		auto sigComp = std::dynamic_pointer_cast<CPS::SimSignalComp>(comp);
		if (sigComp) sigComp->setBehaviour(SimSignalComp::Behaviour::Simulation);
	}

	// Initialize system matrices and source vector.
	initializeSystem();

	mSLog->info("--- Initialization finished ---");
	mSLog->info("--- Initial system matrices and vectors ---");
	logSystemMatrices();

	mSLog->flush();
}

template <>
void MnaSolver<Real>::initializeComponents() {
	mSLog->info("-- Initialize components from power flow");
	mSLog->info("-- o MNA components");
	for (auto comp : mMNAComponents) {
		auto pComp = std::dynamic_pointer_cast<SimPowerComp<Real>>(comp);
		if (!pComp)	continue;
		pComp->initializeFromPowerflow(mSystem.mSystemFrequency);
	}

	/*
	mSLog->info("-- o VarElem components");
	for (auto comp : mVarElems) {
		auto pComp = std::dynamic_pointer_cast<SimPowerComp<Real>>(comp);
		if (!pComp)	continue;
		pComp->initializeFromPowerflow(mSystem.mSystemFrequency);
	}
	*/

	// Initialize signal components.
	for (auto comp : mSimSignalComps)
		comp->initialize(mSystem.mSystemOmega, mTimeStep);

	// Initialize MNA specific parts of components.
	mSLog->info("-- Initialize MNA properties of components");
	mSLog->info("-- o MNA components");
	for (auto comp : mMNAComponents) {
		comp->mnaInitialize(mSystem.mSystemOmega, mTimeStep, attribute<Matrix>("left_vector"));
		const Matrix& stamp = comp->template attribute<Matrix>("right_vector")->get();
		if (stamp.size() != 0) {
			mRightVectorStamps.push_back(&stamp);
		}
	}
	

	mSLog->info("-- o VarElem components");
	mSLog->info("-- o Switches");
	for (auto comp : mSwitches)
		comp->mnaInitialize(mSystem.mSystemOmega, mTimeStep, attribute<Matrix>("left_vector"));

	//mSLog->info("-- o VarElem Switches");
	//for (auto comp : mVarElemsSwitches)
		//comp->mnaInitialize(mSystem.mSystemOmega, mTimeStep, attribute<Matrix>("left_vector"));
}

template <>
void MnaSolver<Complex>::initializeComponents() {
	mSLog->info("-- Initialize components from power flow");
	mSLog->info("-- o MNA components");
	// Initialize power components with frequencies and from powerflow results
	for (auto comp : mMNAComponents) {
		auto pComp = std::dynamic_pointer_cast<SimPowerComp<Complex>>(comp);
		if (!pComp)	continue;
		pComp->initializeFromPowerflow(mSystem.mSystemFrequency);
	}
	/*
	mSLog->info("-- o VarElem components");
	for (auto comp : mVarElems) {
		auto pComp = std::dynamic_pointer_cast<SimPowerComp<Complex>>(comp);
		if (!pComp)	continue;
		pComp->initializeFromPowerflow(mSystem.mSystemFrequency);
	}
	*/

	// Initialize signal components.
	for (auto comp : mSimSignalComps)
		comp->initialize(mSystem.mSystemOmega, mTimeStep);

	mSLog->info("-- Initialize MNA properties of components");
	if (mFrequencyParallel) {
		// Initialize MNA specific parts of components.
		for (auto comp : mMNAComponents) {
			// Initialize MNA specific parts of components.
			comp->mnaInitializeHarm(mSystem.mSystemOmega, mTimeStep, mLeftVectorHarmAttributes);
			const Matrix& stamp = comp->template attribute<Matrix>("right_vector")->get();
			if (stamp.size() != 0) mRightVectorStamps.push_back(&stamp);
		}
		// Initialize nodes
		for (UInt nodeIdx = 0; nodeIdx < mNodes.size(); nodeIdx++) {
			mNodes[nodeIdx]->mnaInitializeHarm(mLeftVectorHarmAttributes);
		}
	}
	else {
		// Initialize MNA specific parts of components.
		mSLog->info("-- o MNA components");
		for (auto comp : mMNAComponents) {
			comp->mnaInitialize(mSystem.mSystemOmega, mTimeStep, attribute<Matrix>("left_vector"));
			const Matrix& stamp = comp->template attribute<Matrix>("right_vector")->get();
			if (stamp.size() != 0) {
				auto idObj = std::dynamic_pointer_cast<IdentifiedObject>(comp);
				mSLog->info("Adding {:s} {:s} into stamping of RHS Vector",
					idObj->type(), idObj->name());
				mRightVectorStamps.push_back(&stamp);
			}
		}
		/*
		mSLog->info("-- o VarElem components");
		for (auto comp : mVarElems) {
			comp->mnaInitialize(mSystem.mSystemOmega, mTimeStep, attribute<Matrix>("left_vector"));
			const Matrix& stamp = comp->template attribute<Matrix>("right_vector")->get();
			if (stamp.size() != 0) {
				mRightVectorStamps.push_back(&stamp);
			}
		}
		*/

		for (auto comp : mSwitches)
			comp->mnaInitialize(mSystem.mSystemOmega, mTimeStep, attribute<Matrix>("left_vector"));
		//for (auto comp : mVarElemsSwitches)
			//comp->mnaInitialize(mSystem.mSystemOmega, mTimeStep, attribute<Matrix>("left_vector"));
	}
}

template <typename VarType>
void MnaSolver<VarType>::initializeSystem() {
	mSLog->info("-- Initialize MNA system matrices and source vector");
	mRightSideVector.setZero();

	if (mFrequencyParallel) {
		/* just a sanity check in case we change the static
		 * initialization of the switch number in the future */
		if (mSwitches.size() > sizeof(std::size_t)*8) {
			throw SystemError("Too many Switches.");
		}
		/* iterate over all possible switch state combinations */
		for (std::size_t i = 0; i < (1ULL << mSwitches.size()); i++) {
			for(Int freq = 0; freq < mSystem.mFrequencies.size(); freq++) {
				mSwitchedMatricesHarm[std::bitset<SWITCH_NUM>(i)][freq].setZero();
			}
		}
	} else {
		for (std::size_t i = 0; i < (1ULL << mSwitches.size()); i++) {
			mSwitchedMatrices[std::bitset<SWITCH_NUM>(i)].setZero();
		}
	}

	if (mFrequencyParallel) {
		for(Int freq = 0; freq < mSystem.mFrequencies.size(); freq++) {
			// Create system matrix if no switches were added
			for (auto comp : mMNAComponents)
				comp->mnaApplySystemMatrixStampHarm(mSwitchedMatricesHarm[std::bitset<SWITCH_NUM>(0)][freq], freq);

			mLuFactorizationsHarm[std::bitset<SWITCH_NUM>(0)].push_back(
				Eigen::PartialPivLU<Matrix>(mSwitchedMatricesHarm[std::bitset<SWITCH_NUM>(0)][freq]));

			// Initialize source vector
			for (auto comp : mMNAComponents)
				comp->mnaApplyRightSideVectorStampHarm(mRightSideVectorHarm[freq], freq);
		}
	}
	else {
		mSLog->info("Number of variable Switches: {} (normal switches: {})"
			"\nNumber of variable Elements: {}"
			"\nNumber of MNA components: {}",
			mVarElemsSwitches.size(),
			mSwitches.size(),
			mVarElems.size(),
			mMNAComponents.size());
		if (mSwitches.size() < 1) {
			mSLog->info("Network contains no (static/independent) Switches");
			// Create system matrix if no switches were added
			mSLog->info("Stamping MNA Components");
			for (auto comp : mMNAComponents) {
				// do not stamp var comp yet
				auto varcomp = std::dynamic_pointer_cast<MNAVarElemInterface>(comp);
				if (varcomp)
					continue;
				comp->mnaApplySystemMatrixStamp(mSwitchedMatrices[std::bitset<SWITCH_NUM>(0)]);
				auto idObj = std::dynamic_pointer_cast<IdentifiedObject>(comp);
				mSLog->debug("Stamping {:s} {:s} into system matrix",
					idObj->type(), idObj->name());
				if (mSLog->should_log(spdlog::level::trace)) {
					mSLog->trace("\n{:s}",
						Logger::matrixToString(mSwitchedMatrices[std::bitset<SWITCH_NUM>(0)]));
				}
			}
			// save base matrix with only static elements
			mSLog->info("Save Base Matrix");
			mBaseSystemMatrix = mSwitchedMatrices[std::bitset<SWITCH_NUM>(0)];

			// now stamp var elements
			mSLog->info("Stamping VarElem Components");
			for (auto varElem : mVarElems) {
				varElem->mnaApplySystemMatrixStamp(mSwitchedMatrices[std::bitset<SWITCH_NUM>(0)]);
				auto idObj = std::dynamic_pointer_cast<IdentifiedObject>(varElem);
				mSLog->info("Stamping {:s} {:s} into system matrix",
					idObj->type(), idObj->name());
			}

			mSLog->info("Stamping VarElem Components (Switches)");
			for (auto varElem : mVarElemsSwitches) {
				if (varElem) {
					varElem->mnaApplySystemMatrixStamp(mSwitchedMatrices[std::bitset<SWITCH_NUM>(0)]);
					auto idObj = std::dynamic_pointer_cast<IdentifiedObject>(varElem);
					mSLog->info("Stamping {:s} {:s} into system matrix",
						idObj->type(), idObj->name());
				}
			}

			mLuFactorizations[std::bitset<SWITCH_NUM>(0)] = Eigen::PartialPivLU<Matrix>(mSwitchedMatrices[std::bitset<SWITCH_NUM>(0)]);
			// log system matrix
			mSLog->info("Logging intial System Matrix");
			mSysMatrixLog->logPhasorNodeValues(0, mSwitchedMatrices[std::bitset<SWITCH_NUM>(0)]);
		}
		else {
			// Generate switching state dependent system matrices
			for (auto& sys : mSwitchedMatrices) {
				for (auto comp : mMNAComponents)
					comp->mnaApplySystemMatrixStamp(sys.second);
				for (UInt i = 0; i < mSwitches.size(); i++)
					mSwitches[i]->mnaApplySwitchSystemMatrixStamp(sys.second, sys.first[i]);
				// Compute LU-factorization for system matrix
				mLuFactorizations[sys.first] = Eigen::PartialPivLU<Matrix>(sys.second);
			}
			updateSwitchStatus();
		}

		// Initialize source vector for debugging
		mSLog->info("### Stamping MNA comp in Source Vector");
		for (auto comp : mMNAComponents) {
			comp->mnaApplyRightSideVectorStamp(mRightSideVector);

			auto idObj = std::dynamic_pointer_cast<IdentifiedObject>(comp);
			mSLog->info("Stamping {:s} {:s} into source vector (mna comp)",
				idObj->type(), idObj->name());

			if (mSLog->should_log(spdlog::level::trace)) {
				mSLog->trace("\n{:s}", Logger::matrixToString(mRightSideVector));
			}

		}

		/*
		mSLog->info("### Stamping VarElem comp in Source Vector");
		for (auto comp : mVarElems) {
			comp->mnaApplyRightSideVectorStamp(mRightSideVector);
			auto idObj = std::dynamic_pointer_cast<IdentifiedObject>(comp);

			mSLog->info("Stamping {:s} {:s} into source vector (var comp)",
				idObj->type(), idObj->name());
			if (mSLog->should_log(spdlog::level::trace)) {
				mSLog->trace("\n{:s}", Logger::matrixToString(mRightSideVector));
			}
		}
		*/

	}
}

template <typename VarType>
void MnaSolver<VarType>::updateSwitchStatus() {

	std::bitset<SWITCH_NUM> PrevSwitchStatus = mCurrentSwitchStatus;
	for (UInt i = 0; i < mSwitches.size(); i++) {
		mCurrentSwitchStatus.set(i, mSwitches[i]->mnaIsClosed());
		
	}

	if (mCurrentSwitchStatus != PrevSwitchStatus){
		mSLog->info("Switch status changed");
		mUpdateSysMatrix = true;
	}

	/*
	for (auto sw : mSwitches) {
		if (sw->mnaStateChanged())
		{
			mUpdateSysMatrix = true;
			break;
		}
	}
	*/
}

/// new for recalculation of system matrix
template <typename VarType>
void MnaSolver<VarType>::updateVarElemStatus() {
	for (auto varElem : mVarElems) {
		if (varElem->ValueChanged())
		{
			auto idObj = std::dynamic_pointer_cast<IdentifiedObject>(varElem);
			mSLog->info("Component ({:s} {:s}) value changed -> Update System Matrix",
				idObj->type(), idObj->name());
			mUpdateSysMatrix = true;
			mLogSysMatrix = true;
			break;
		}
	}

	if (!mUpdateSysMatrix) {
		for (auto varElem : mVarElemsSwitches) {
			if (varElem) {
				if (varElem->ValueChanged())
				{
					auto idObj = std::dynamic_pointer_cast<IdentifiedObject>(varElem);
					mSLog->info("Switch ({:s} {:s}) value changed -> Update System Matrix",
						idObj->type(), idObj->name());
					mUpdateSysMatrix = true;
					mLogSysMatrix = true;
					break;
				}
			}
		}
	}
}

template <typename VarType>
void MnaSolver<VarType>::updateSystemMatrix(Real time) {
	// reset system matrix
	mSLog->info("Updating System Matrix at {}\n", time);

	/*
	for (std::size_t i = 0; i < (1ULL << mSwitches.size()); i++) {
		mSwitchedMatrices[std::bitset<SWITCH_NUM>(i)].setZero();
	}

	// Generate switching state dependent system matrices
	for (auto& sys : mSwitchedMatrices) {
		for (auto comp : mMNAComponents)
			comp->mnaApplySystemMatrixStamp(sys.second);
		for (UInt i = 0; i < mSwitches.size(); i++)
			mSwitches[i]->mnaApplySwitchSystemMatrixStamp(sys.second, sys.first[i]);
		// Compute LU-factorization for system matrix
		mLuFactorizations[sys.first] = Eigen::PartialPivLU<Matrix>(sys.second);
	}
	updateSwitchStatus();
	*/

	//mSwitchedMatrices[std::bitset<SWITCH_NUM>(0)].setZero();

	// set Base Matrix
	mSwitchedMatrices[std::bitset<SWITCH_NUM>(0)] = mBaseSystemMatrix;

	// Create system matrix with changed variable elements
	for (auto comp : mVarElems) {
		// components
		comp->mnaApplySystemMatrixStamp(mSwitchedMatrices[std::bitset<SWITCH_NUM>(0)]);
		auto idObj = std::dynamic_pointer_cast<IdentifiedObject>(comp);
		mSLog->debug("Updating {:s} {:s} in system matrix (variabel component)",
			idObj->type(), idObj->name());
	}
	// Now switches
	for (auto comp : mVarElemsSwitches) {
		if (comp) {
			// apply stamp. first check if element is actice/exists
			comp->mnaApplySystemMatrixStamp(mSwitchedMatrices[std::bitset<SWITCH_NUM>(0)]);
			auto idObj = std::dynamic_pointer_cast<IdentifiedObject>(comp);
			mSLog->debug("Updating {:s} {:s} in system matrix (variabel switch)",
				idObj->type(), idObj->name());
		}
	}

	mLuFactorizations[std::bitset<SWITCH_NUM>(0)] = Eigen::PartialPivLU<Matrix>(mSwitchedMatrices[std::bitset<SWITCH_NUM>(0)]);
	mUpdateSysMatrix = false;
}

template <typename VarType>
void MnaSolver<VarType>::identifyTopologyObjects() {
	for (auto baseNode : mSystem.mNodes) {
		// Add nodes to the list and ignore ground nodes.
		if (!baseNode->isGround()) {
			auto node = std::dynamic_pointer_cast< CPS::SimNode<VarType> >(baseNode);
			mNodes.push_back( node );
		}
	}

	for (UInt i = 0; i < mNodes.size(); i++)
		mSLog->info("Added node {:s}", mNodes[i]->name());;

	for (auto comp : mSystem.mComponents) {
		auto swComp = std::dynamic_pointer_cast<CPS::MNASwitchInterface>(comp);
		if (swComp) {
			// To Do use continue as those elements shouldnt be stamped in normal (base) matrix
			//continue;
			mSwitches.push_back(swComp);
			auto idObj = std::dynamic_pointer_cast<IdentifiedObject>(swComp);
			mSLog->info("Adding {:s} {:s} in List of Switch Elements",
				idObj->type(), idObj->name());
			continue;
		}

		// is transformers oltc?
		/*
		auto oltcComp = std::dynamic_pointer_cast<CPS::MNAOLTCInterface>(comp);
		if (oltcComp)
		{
			mOLTCs.push_back(oltcComp);
			continue;
		}
		*/

		// has element variable parameters
		auto varComp = std::dynamic_pointer_cast<CPS::MNAVarElemInterface>(comp);
		if (varComp)
		{
			auto idObj = std::dynamic_pointer_cast<IdentifiedObject>(varComp);
			mSLog->info("Adding {:s} {:s} in List of Variable Elements",
				idObj->type(), idObj->name());

			mVarElems.push_back(varComp);
			//continue;
		}

		auto mnaComp = std::dynamic_pointer_cast<CPS::MNAInterface>(comp);
		if (mnaComp) {
			auto idObj = std::dynamic_pointer_cast<IdentifiedObject>(mnaComp);
			mSLog->info("Adding {:s} {:s} in List of MNA Elements",
				idObj->type(), idObj->name());
			mMNAComponents.push_back(mnaComp);
		}

		auto sigComp = std::dynamic_pointer_cast<CPS::SimSignalComp>(comp);
		if (sigComp) mSimSignalComps.push_back(sigComp);
	}
}

template <typename VarType>
void MnaSolver<VarType>::assignMatrixNodeIndices() {
	UInt matrixNodeIndexIdx = 0;
	for (UInt idx = 0; idx < mNodes.size(); idx++) {
		mNodes[idx]->setMatrixNodeIndex(0, matrixNodeIndexIdx);
		matrixNodeIndexIdx++;
		if (mNodes[idx]->phaseType() == CPS::PhaseType::ABC) {
			mNodes[idx]->setMatrixNodeIndex(1, matrixNodeIndexIdx);
			matrixNodeIndexIdx++;
			mNodes[idx]->setMatrixNodeIndex(2, matrixNodeIndexIdx);
			matrixNodeIndexIdx++;
		}
		if (idx == mNumNetNodes-1) mNumNetMatrixNodeIndices = matrixNodeIndexIdx;
	}
	// Total number of network nodes is matrixNodeIndexIdx + 1
	mNumMatrixNodeIndices = matrixNodeIndexIdx;
	mNumVirtualMatrixNodeIndices = mNumMatrixNodeIndices - mNumNetMatrixNodeIndices;
	mNumHarmMatrixNodeIndices = static_cast<UInt>(mSystem.mFrequencies.size()-1) * mNumMatrixNodeIndices;

	mSLog->info("Assigned simulation nodes to topology nodes:");
	mSLog->info("Number of network simulation nodes: {:d}", mNumNetMatrixNodeIndices);
	mSLog->info("Number of simulation nodes: {:d}", mNumMatrixNodeIndices);
	mSLog->info("Number of harmonic simulation nodes: {:d}", mNumHarmMatrixNodeIndices);
}

template<>
void MnaSolver<Real>::createEmptyVectors() {
	mRightSideVector = Matrix::Zero(mNumMatrixNodeIndices, 1);
	mLeftSideVector = Matrix::Zero(mNumMatrixNodeIndices, 1);
}

template<>
void MnaSolver<Complex>::createEmptyVectors() {
	if (mFrequencyParallel) {
		for(Int freq = 0; freq < mSystem.mFrequencies.size(); freq++) {
			mRightSideVectorHarm.push_back(Matrix::Zero(2*(mNumMatrixNodeIndices), 1));
			mLeftSideVectorHarm.push_back(Matrix::Zero(2*(mNumMatrixNodeIndices), 1));
		}
	}
	else {
		mRightSideVector = Matrix::Zero(2*(mNumMatrixNodeIndices + mNumHarmMatrixNodeIndices), 1);
		mLeftSideVector = Matrix::Zero(2*(mNumMatrixNodeIndices + mNumHarmMatrixNodeIndices), 1);
	}
}

template<>
void MnaSolver<Real>::createEmptySystemMatrix() {
	if (mSwitches.size() > SWITCH_NUM)
		throw SystemError("Too many Switches.");

	for (std::size_t i = 0; i < (1ULL << mSwitches.size()); i++) {
		mSwitchedMatrices[std::bitset<SWITCH_NUM>(i)] = Matrix::Zero(mNumMatrixNodeIndices, mNumMatrixNodeIndices);
	}
	// create Base Matrix
	mSLog->info("Save Base Matrix (Real) with Zeros (Initial)");
	mBaseSystemMatrix = Matrix::Zero(mNumMatrixNodeIndices, mNumMatrixNodeIndices);
}

template<>
void MnaSolver<Complex>::createEmptySystemMatrix() {
	if (mSwitches.size() > SWITCH_NUM)
		throw SystemError("Too many Switches.");

	if (mFrequencyParallel) {
		for (UInt i = 0; i < std::pow(2,mSwitches.size()); i++) {
			for(Int freq = 0; freq < mSystem.mFrequencies.size(); freq++) {
				mSwitchedMatricesHarm[std::bitset<SWITCH_NUM>(i)].push_back(
					Matrix::Zero(2*(mNumMatrixNodeIndices), 2*(mNumMatrixNodeIndices)));
			}
		}
	}
	else {
		for (std::size_t i = 0; i < (1ULL << mSwitches.size()); i++) {
			mSwitchedMatrices[std::bitset<SWITCH_NUM>(i)] = Matrix::Zero(2*(mNumMatrixNodeIndices + mNumHarmMatrixNodeIndices), 2*(mNumMatrixNodeIndices + mNumHarmMatrixNodeIndices));
		}
		// create Base Matrix
		mSLog->info("Save Base Matrix (Complex) with Zeros (Initial)");
		mBaseSystemMatrix = Matrix::Zero(2 * (mNumMatrixNodeIndices + mNumHarmMatrixNodeIndices), 2 * (mNumMatrixNodeIndices + mNumHarmMatrixNodeIndices));
	}
}

template <typename VarType>
void MnaSolver<VarType>::createVirtualNodes() {
	// We have not added virtual nodes yet so the list has only network nodes
	mNumNetNodes = (UInt) mNodes.size();
	// virtual nodes are placed after network nodes
	UInt virtualNode = mNumNetNodes - 1;
	// Check if component requires virtual node and if so set one
	for (auto comp : mMNAComponents) {
		auto pComp = std::dynamic_pointer_cast<SimPowerComp<VarType>>(comp);
		if (!pComp)	continue;

		if (pComp->hasVirtualNodes()) {
			for (UInt node = 0; node < pComp->virtualNodesNumber(); node++) {
					mNodes.push_back(pComp->virtualNode(node));
					if (pComp->virtualNode(node)->phaseType() != CPS::PhaseType::ABC) {
						virtualNode++;
						pComp->virtualNode(node)->setMatrixNodeIndex(0, virtualNode);
						mSLog->info("Assigned index {} to virtual node {} for {}", virtualNode, node, pComp->name());
					} else {
						for (UInt phase = 0; phase < 3; phase++) {
							virtualNode++;
							pComp->virtualNode(node)->setMatrixNodeIndex(phase, virtualNode);
							mSLog->info("Assigned index {} to phase {} of virtual node {} for {}", virtualNode, phase, node, pComp->name());
						}
					}
				}
		}

		if (pComp->hasSubComponents()) {
			for (auto pSubComp : pComp->subComponents()) {
				for (UInt node = 0; node < pSubComp->virtualNodesNumber(); node++) {
					mNodes.push_back(pSubComp->virtualNode(node));
					if (pSubComp->virtualNode(node)->phaseType() != CPS::PhaseType::ABC) {
						virtualNode++;
						pSubComp->virtualNode(node)->setMatrixNodeIndex(0, virtualNode);
						mSLog->info("Assigned index {} to virtual node {} for {}", virtualNode, node, pSubComp->name());
					} else {
						for (UInt phase = 0; phase < 3; phase++) {
							virtualNode++;
							pSubComp->virtualNode(node)->setMatrixNodeIndex(phase, virtualNode);
							mSLog->info("Assigned index {} to phase {} of virtual node {} for {}", virtualNode, phase, node, pSubComp->name());
						}
					}
				}
			}
		}
	}

	/*
	for (auto comp : mVarElems) {
		auto pComp = std::dynamic_pointer_cast<SimPowerComp<VarType>>(comp);
		if (!pComp)	continue;

		if (pComp->hasVirtualNodes()) {
			for (UInt node = 0; node < pComp->virtualNodesNumber(); node++) {
				mNodes.push_back(pComp->virtualNode(node));
				if (pComp->virtualNode(node)->phaseType() != CPS::PhaseType::ABC) {
					virtualNode++;
					pComp->virtualNode(node)->setMatrixNodeIndex(0, virtualNode);
					mSLog->info("Assigned index {} to virtual node {} for {}", virtualNode, node, pComp->name());
				}
				else {
					for (UInt phase = 0; phase < 3; phase++) {
						virtualNode++;
						pComp->virtualNode(node)->setMatrixNodeIndex(phase, virtualNode);
						mSLog->info("Assigned index {} to phase {} of virtual node {} for {}", virtualNode, phase, node, pComp->name());
					}
				}
			}
		}

		if (pComp->hasSubComponents()) {
			for (auto pSubComp : pComp->subComponents()) {
				for (UInt node = 0; node < pSubComp->virtualNodesNumber(); node++) {
					mNodes.push_back(pSubComp->virtualNode(node));
					if (pSubComp->virtualNode(node)->phaseType() != CPS::PhaseType::ABC) {
						virtualNode++;
						pSubComp->virtualNode(node)->setMatrixNodeIndex(0, virtualNode);
						mSLog->info("Assigned index {} to virtual node {} for {}", virtualNode, node, pSubComp->name());
					}
					else {
						for (UInt phase = 0; phase < 3; phase++) {
							virtualNode++;
							pSubComp->virtualNode(node)->setMatrixNodeIndex(phase, virtualNode);
							mSLog->info("Assigned index {} to phase {} of virtual node {} for {}", virtualNode, phase, node, pSubComp->name());
						}
					}
				}
			}
		}
	}
	*/

	// Update node number to create matrices and vectors
	mNumNodes = (UInt) mNodes.size();
	mNumVirtualNodes = mNumNodes - mNumNetNodes;
	mSLog->info("Created virtual nodes:");
	mSLog->info("Number of network nodes: {:d}", mNumNetNodes);
	mSLog->info("Number of network and virtual nodes: {:d}", mNumNodes);
}

template <typename VarType>
void MnaSolver<VarType>::steadyStateInitialization() {
	mSLog->info("--- Run steady-state initialization ---");

	DataLogger initLeftVectorLog(mName + "_InitLeftVector", mLogLevel != CPS::Logger::Level::off);
	DataLogger initRightVectorLog(mName + "_InitRightVector", mLogLevel != CPS::Logger::Level::off);

	TopologicalPowerComp::Behaviour initBehaviourPowerComps = TopologicalPowerComp::Behaviour::Initialization;
	SimSignalComp::Behaviour initBehaviourSignalComps = SimSignalComp::Behaviour::Initialization;

	// TODO: enable use of timestep distinct from simulation timestep
	Real initTimeStep = mTimeStep;

	Int timeStepCount = 0;
	Real time = 0;
	Real maxDiff = 1.0;
	Real max = 1.0;
	Matrix diff = Matrix::Zero(2 * mNumNodes, 1);
	Matrix prevLeftSideVector = Matrix::Zero(2 * mNumNodes, 1);

	mSLog->info("Time step is {:f}s for steady-state initialization", initTimeStep);

	for (auto comp : mSystem.mComponents) {
		auto powerComp = std::dynamic_pointer_cast<CPS::TopologicalPowerComp>(comp);
		if (powerComp) powerComp->setBehaviour(initBehaviourPowerComps);

		auto sigComp = std::dynamic_pointer_cast<CPS::SimSignalComp>(comp);
		if (sigComp) sigComp->setBehaviour(initBehaviourSignalComps);
	}

	initializeSystem();
	logSystemMatrices();

	updateSwitchStatus();

	// Use sequential scheduler
	SequentialScheduler sched;
	CPS::Task::List tasks;
	Scheduler::Edges inEdges, outEdges;

	for (auto node : mNodes) {
		for (auto task : node->mnaTasks())
			tasks.push_back(task);
	}
	for (auto comp : mMNAComponents) {
		for (auto task : comp->mnaTasks()) {
			tasks.push_back(task);
		}
	}
	/*
	for (auto comp : mVarElems) {
		for (auto task : comp->mnaTasks()) {
			tasks.push_back(task);
		}
	}
	*/

	// TODO signal components should be moved out of MNA solver
	for (auto comp : mSimSignalComps) {
		for (auto task : comp->getTasks()) {
			tasks.push_back(task);
		}
	}
	tasks.push_back(std::make_shared<MnaSolver<VarType>::SolveTask>(*this, true));

	sched.resolveDeps(tasks, inEdges, outEdges);
	sched.createSchedule(tasks, inEdges, outEdges);

	while (time < mSteadStIniTimeLimit) {
		// Reset source vector
		mRightSideVector.setZero();

		sched.step(time, timeStepCount);

		if (mDomain == CPS::Domain::EMT) {
			initLeftVectorLog.logEMTNodeValues(time, leftSideVector());
			initRightVectorLog.logEMTNodeValues(time, rightSideVector());
		}
		else {
			initLeftVectorLog.logPhasorNodeValues(time, leftSideVector());
			initRightVectorLog.logPhasorNodeValues(time, rightSideVector());
		}

		// Calculate new simulation time
		time = time + initTimeStep;
		timeStepCount++;

		// Calculate difference
		diff = prevLeftSideVector - mLeftSideVector;
		prevLeftSideVector = mLeftSideVector;
		maxDiff = diff.lpNorm<Eigen::Infinity>();
		max = mLeftSideVector.lpNorm<Eigen::Infinity>();
		// If difference is smaller than some epsilon, break
		if ((maxDiff / max) < mSteadStIniAccLimit)
			break;
	}

	mSLog->info("Max difference: {:f} or {:f}% at time {:f}", maxDiff, maxDiff / max, time);

	// Reset system for actual simulation
	mRightSideVector.setZero();

	mSLog->info("--- Finished steady-state initialization ---");
}

template <typename VarType>
Task::List MnaSolver<VarType>::getTasks() {
	Task::List l;

	for (auto comp : mMNAComponents) {
		for (auto task : comp->mnaTasks()) {
			l.push_back(task);
		}
	}
	/*
	for (auto comp : mVarElems) {
		for (auto task : comp->mnaTasks()) {
			l.push_back(task);
		}
	}
	*/

	for (auto node : mNodes) {
		for (auto task : node->mnaTasks())
			l.push_back(task);
	}
	// TODO signal components should be moved out of MNA solver
	for (auto comp : mSimSignalComps) {
		for (auto task : comp->getTasks()) {
			l.push_back(task);
		}
	}
	if (mFrequencyParallel) {
		for (UInt i = 0; i < mSystem.mFrequencies.size(); i++)
			l.push_back(std::make_shared<MnaSolver<VarType>::SolveTaskHarm>(*this, false, i));
	} else {
		l.push_back(std::make_shared<MnaSolver<VarType>::SolveTask>(*this, false));
		l.push_back(std::make_shared<MnaSolver<VarType>::LogTask>(*this));
	}
	return l;
}

template <typename VarType>
void MnaSolver<VarType>::SolveTask::execute(Real time, Int timeStepCount) {
	// Reset source vector
	mSolver.mRightSideVector.setZero();
	mSolver.mUpdateSysMatrix = false;

	// Add together the right side vector (computed by the components'
	// pre-step tasks)
	for (auto stamp : mSolver.mRightVectorStamps)
		mSolver.mRightSideVector += *stamp;

	if (mSolver.mSwitchedMatrices.size() > 0)
		mSolver.mLeftSideVector = mSolver.mLuFactorizations[mSolver.mCurrentSwitchStatus].solve(mSolver.mRightSideVector);

	// TODO split into separate task? (dependent on x, updating all v attributes)
	for (UInt nodeIdx = 0; nodeIdx < mSolver.mNumNetNodes; nodeIdx++)
		mSolver.mNodes[nodeIdx]->mnaUpdateVoltage(mSolver.mLeftSideVector);

	//if (!mSteadyStateInit)
		//mSolver.updateSwitchStatus();

	if ((mSolver.mOLTCs.size() > 0) || (mSolver.mVarElems.size() > 0) || (mSolver.mVarElemsSwitches.size() > 0))
	{
		// update OLTCs
		//mSolver.updateOLTCStatus();

		// update switches
		//mSolver.updateSwitchStatus();

		mSolver.updateVarElemStatus();
		if (mSolver.mUpdateSysMatrix) {
			mSolver.updateSystemMatrix(time);
		}
	}

	// Components' states will be updated by the post-step tasks

}

template <typename VarType>
void MnaSolver<VarType>::SolveTaskHarm::execute(Real time, Int timeStepCount) {
	mSolver.mRightSideVectorHarm[mFreqIdx].setZero();

	// Add together the right side vector (computed by the components'
	// pre-step tasks)
	for (auto stamp : mSolver.mRightVectorStamps)
		mSolver.mRightSideVectorHarm[mFreqIdx] += stamp->col(mFreqIdx);

	mSolver.mLeftSideVectorHarm[mFreqIdx] =
		mSolver.mLuFactorizationsHarm[mSolver.mCurrentSwitchStatus][mFreqIdx].solve(mSolver.mRightSideVectorHarm[mFreqIdx]);
}

template <typename VarType>
void MnaSolver<VarType>::log(Real time) {
	if (mLogLevel == Logger::Level::off)
		return;

	if (mDomain == CPS::Domain::EMT) {
		mLeftVectorLog->logEMTNodeValues(time, leftSideVector());
		mRightVectorLog->logEMTNodeValues(time, rightSideVector());
	}
	else {
		mLeftVectorLog->logPhasorNodeValues(time, leftSideVector());
		mRightVectorLog->logPhasorNodeValues(time, rightSideVector());
		if (mLogSysMatrix) {
			mSysMatrixLog->logPhasorNodeValues(time, mSwitchedMatrices[std::bitset<SWITCH_NUM>(0)]);
			mLogSysMatrix = false;
		}
	}
}

template <typename VarType>
void MnaSolver<VarType>::logSystemMatrices() {
	if (mFrequencyParallel) {
		for (UInt i = 0; i < mSwitchedMatricesHarm[std::bitset<SWITCH_NUM>(0)].size(); i++) {
			mSLog->info("System matrix for frequency: {:d} \n{:s}", i,
				Logger::matrixToString(mSwitchedMatricesHarm[std::bitset<SWITCH_NUM>(0)][i]));
			mSLog->info("LU decomposition for frequency: {:d} \n{:s}", i,
				Logger::matrixToString(mLuFactorizationsHarm[std::bitset<SWITCH_NUM>(0)][i].matrixLU()));
		}

		for (UInt i = 0; i < mRightSideVectorHarm.size(); i++)
			mSLog->info("Right side vector for frequency: {:d} \n{:s}", i,
				Logger::matrixToString(mRightSideVectorHarm[i]));

	}
	else {
		if (mSwitches.size() < 1) {
			mSLog->info("System matrix: \n{}", mSwitchedMatrices[std::bitset<SWITCH_NUM>(0)]);
			mSLog->info("LU decomposition: \n{}",	mLuFactorizations[std::bitset<SWITCH_NUM>(0)].matrixLU());
		}
		else {
			mSLog->info("Initial switch status: {:s}", mCurrentSwitchStatus.to_string());

			for (auto sys : mSwitchedMatrices) {
				mSLog->info("Switching System matrix {:s} \n{:s}",
					sys.first.to_string(), Logger::matrixToString(sys.second));
				mSLog->info("LU Factorization for System Matrix {:s} \n{:s}",
					sys.first.to_string(), Logger::matrixToString(mLuFactorizations[sys.first].matrixLU()));
			}
		}
		mSLog->info("Right side vector: \n{}", mRightSideVector);
	}
}

template <typename VarType>
void MnaSolver<VarType>::LogTask::execute(Real time, Int timeStepCount) {
	mSolver.log(time);
}

}

template class DPsim::MnaSolver<Real>;
template class DPsim::MnaSolver<Complex>;
