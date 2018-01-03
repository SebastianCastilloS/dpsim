/** Read CIM files
 *
 * @file
 * @author Georg Reinke <georg.reinke@rwth-aachen.de>
 * @copyright 2017, Institute for Automation of Complex Power Systems, EONERC
 *
 * DPsim
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *********************************************************************************/

#pragma once

#include <map>
#include <vector>

#include "Definitions.h"
#include "Components/BaseComponent.h"

#include "CIMModel.hpp"
#include "IEC61970.hpp"
#include "Logger.h"
#include "Simulation.h"

using namespace IEC61970::Base::Domain;
using namespace IEC61970::Base::Equivalents;
using namespace IEC61970::Base::StateVariables;
using namespace IEC61970::Base::Topology;
using namespace IEC61970::Base::Wires;

namespace DPsim {
namespace CIM {

	class Reader {
		private:
			/// CIM logger
			Logger* mLogger;
			/// Model from CIM++
			CIMModel mModel;
			/// All components after mapping
			BaseComponent::List mComponents;
			/// System frequency (has to be given to convert between reactances
			/// in CIM and inductances used inside the simulation)
			Real mFrequency;
			// Maps the RID of a topological node to its simulation matrix index
			// as given in the component constructors (1 for the first node).
			std::map<String, Matrix::Index> mTopNodes;
			// Maps the RID of a ConductingEquipment to a list of nodes as given in
			// the component constructors.
			std::map<String, std::vector<Matrix::Index>> mEqNodeMap;
			// SvVoltage, if present, for each node (indexed starting with 0!)
			SvVoltage **mVoltages;
			/// Maps the RID of a Terminal to its associated power flow
			std::map<String, SvPowerFlow*> mPowerFlows;
			/// Number of ideal voltage sources
			Int mNumVoltageSources;

			BaseComponent::Ptr mapComponent(BaseClass* obj);

			/// Returns an RX-Line.
			/// The voltage should be given in kV and the angle in degree.
			/// TODO: Introduce different models such as PI and wave model.
			BaseComponent::Ptr mapACLineSegment(ACLineSegment* line);
			void mapAsynchronousMachine(AsynchronousMachine* machine);
			/// Returns an PQload with voltage setting according to load flow data.
			/// Currently the only option is to create an RL-load.
			/// The voltage should be given in kV and the angle in degree.
			/// TODO: Introduce different load models here.
			void mapEnergyConsumer(EnergyConsumer* con);
			void mapEquivalentInjection(EquivalentInjection* inj);
			BaseComponent::Ptr mapExternalNetworkInjection(ExternalNetworkInjection* inj);
			BaseComponent::Ptr mapPowerTransformer(PowerTransformer *trans);
			/// Returns an IdealVoltageSource with voltage setting according to load flow data
			/// at machine terminals. The voltage should be given in kV and the angle in degree.
			/// TODO: Introduce real synchronous generator models here.
			BaseComponent::Ptr mapSynchronousMachine(SynchronousMachine* machine);
			/// Returns an PQload with voltage setting according to load flow data.
			/// Currently the only option is to create an RL-load.
			/// The voltage should be given in kV and the angle in degree.
			/// TODO: Introduce real PQload model here.
			BaseComponent::Ptr newPQLoad(String rid, String name);
		public:
			Reader(Real om, Logger& logger);
			virtual ~Reader();

			bool addFile(String filename);
			void parseFiles();
			BaseComponent::List& getComponents();
			Matrix::Index mapTopologicalNode(String mrid);
			Int getNumVoltageSources();

			static Real unitValue(Real value, UnitMultiplier mult);
	};
};
};