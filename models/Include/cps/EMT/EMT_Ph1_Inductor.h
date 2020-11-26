/* Copyright 2017-2020 Institute for Automation of Complex Power Systems,
 *                     EONERC, RWTH Aachen University
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *********************************************************************************/

#pragma once

#include <cps/SimPowerComp.h>
#include <cps/Solver/MNAInterface.h>
#include <cps/Solver/DAEInterface.h>
#include <cps/Base/Base_Ph1_Inductor.h>

namespace CPS {
namespace EMT {
namespace Ph1 {
	/// \brief Inductor
	///
	/// The inductor is represented by a DC equivalent circuit which corresponds to
	/// one iteration of the trapezoidal integration method.
	/// The equivalent DC circuit is a resistance in paralel with a current source.
	/// The resistance is constant for a defined time step and system
	/// frequency and the current source changes for each iteration.
	class Inductor :
		public Base::Ph1::Inductor,
		public DAEInterface,
		public MNAInterface,
		public SimPowerComp<Real>,
		public SharedFactory<Inductor> {
	protected:
		/// DC equivalent current source [A]
		Real mEquivCurrent;
		/// Equivalent conductance [S]
		Real mEquivCond;
	public:
		/// Defines UID, name, component parameters and logging level
		Inductor(String uid, String name, Logger::Level logLevel = Logger::Level::info);
		/// Defines name and logging level
		Inductor(String name, Logger::Level logLevel = Logger::Level::info)
			: Inductor(name, name, logLevel) { }

		SimPowerComp<Real>::Ptr clone(String name);

		// #### General ####
		/// Initializes component from power flow data
		void initializeFromNodesAndTerminals(Real frequency);

		// #### MNA section ####
		/// Initializes internal variables of the component
		void mnaInitialize(Real omega, Real timeStep, Attribute<Matrix>::Ptr leftVector);
		/// Stamps system matrix
		void mnaApplySystemMatrixStamp(Matrix& systemMatrix);
		/// Stamps right side (source) vector
		void mnaApplyRightSideVectorStamp(Matrix& rightVector);
		/// Update interface voltage from MNA system result
		void mnaUpdateVoltage(const Matrix& leftVector);
		/// Update interface current from MNA system result
		void mnaUpdateCurrent(const Matrix& leftVector);

		class MnaPreStep : public Task {
		public:
			MnaPreStep(Inductor& inductor) :
				Task(inductor.mName + ".MnaPreStep"), mInductor(inductor) {
				// actually depends on L, but then we'd have to modify the system matrix anyway
				mModifiedAttributes.push_back(inductor.attribute("right_vector"));
				mPrevStepDependencies.push_back(inductor.attribute("i_intf"));
				mPrevStepDependencies.push_back(inductor.attribute("v_intf"));
			}

			void execute(Real time, Int timeStepCount);

		private:
			Inductor& mInductor;
		};

		class MnaPostStep : public Task {
		public:
			MnaPostStep(Inductor& inductor, Attribute<Matrix>::Ptr leftVector) :
				Task(inductor.mName + ".MnaPostStep"), mInductor(inductor), mLeftVector(leftVector) {
				mAttributeDependencies.push_back(mLeftVector);
				mModifiedAttributes.push_back(mInductor.attribute("v_intf"));
				mModifiedAttributes.push_back(mInductor.attribute("i_intf"));
			}

			void execute(Real time, Int timeStepCount);

		private:
			Inductor& mInductor;
			Attribute<Matrix>::Ptr mLeftVector;
		};

		// #### DAE Section ####
		///
		void daeInitialize(double time, double state[], double dstate_dt[], int& counter);
		///
		void daePreStep(double time){};
		///Residual Function for DAE Solver
		void daeResidual(double time, const double state[], const double dstate_dt[], double resid[], std::vector<int>& off);
		///
		void daePostStep(double Nexttime, const double state[], const double dstate_dt[], int& counter);
		///
		int getNumberOfStateVariables() {return 1;}
	};
}
}
}
