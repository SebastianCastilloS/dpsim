/** Python binding for Capacitors.
 *
 * @author Steffen Vogel <stvogel@eonerc.rwth-aachen.de>
 * @copyright 2017, Institute for Automation of Complex Power Systems, EONERC
 *
 * CPowerSystems
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

#include "Python/Component.h"
#include "Python/Node.h"
#include "cps/Components/DP_Capacitor.h"
#include "cps/Components/EMT_Capacitor.h"

namespace CPS {
namespace Python {
namespace Components {

	extern const char* DocCapacitor;

	template<class C>
	PyObject* Capacitor(PyObject* self, PyObject* args);

	template<>
	PyObject* Capacitor<CPS::Components::EMT::Capacitor>(PyObject* self, PyObject* args)
	{
		const char *name;
		double capacitance;

		PyObject *pyNodes;

		if (!PyArg_ParseTuple(args, "sOd", &name, &pyNodes, &capacitance))
			return nullptr;

		try {
			CPS::Node<Real>::List nodes = Python::Node<Real>::fromPython(pyNodes);

			Component *pyComp = PyObject_New(Component, &CPS::Python::ComponentType);
			Component::init(pyComp);
			pyComp->comp = std::make_shared<CPS::Components::EMT::Capacitor>(name, nodes, capacitance);

			return (PyObject*) pyComp;
		} catch (...) {
			return nullptr;
		}
	}

	template<>
	PyObject* Capacitor<CPS::Components::DP::Capacitor>(PyObject* self, PyObject* args)
	{
		const char *name;
		double capacitance;

		PyObject *pyNodes;

		if (!PyArg_ParseTuple(args, "sOd", &name, &pyNodes, &capacitance))
			return nullptr;

		try {
			CPS::Node<Complex>::List nodes = Python::Node<Complex>::fromPython(pyNodes);

			Component *pyComp = PyObject_New(Component, &CPS::Python::ComponentType);
			Component::init(pyComp);
			pyComp->comp = std::make_shared<CPS::Components::DP::Capacitor>(name, nodes, capacitance);

			return (PyObject*) pyComp;
		} catch (...) {
			return nullptr;
		}
	}
}
}
}