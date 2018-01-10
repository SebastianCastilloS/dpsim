/** Reference Circuits
 *
 * @author Markus Mirz <mmirz@eonerc.rwth-aachen.de>
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

#include "Simulation.h"
#include "Utilities.h"

using namespace DPsim;
using namespace DPsim::Components::EMT;

int main(int argc, char* argv[])
{
	// Define simulation scenario
	Real timeStep = 0.001;
	Real omega = 2.0*M_PI*50.0;
	Real finalTime = 0.3;
	String simName = "IdealVS_EMT_" + std::to_string(timeStep);

	Components::Base::List circElements = {
		VoltageSourceIdeal::make("v_in", 1, 2, 10),
		Resistor::make("r_1", 1, 0, 5),
		Resistor::make("r_2", 2, 0, 10),
		Resistor::make("r_3", 2, 0, 2)
	};

	// Set up simulation and start main simulation loop
	Simulation newSim(simName, circElements, omega, timeStep, finalTime, Logger::Level::INFO, SimulationType::EMT);

	std::cout << "Start simulation." << std::endl;
	newSim.run();
	std::cout << "Simulation finished." << std::endl;

	return 0;
}
