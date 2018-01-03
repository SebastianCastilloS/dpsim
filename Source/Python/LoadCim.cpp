/** Python components
 *
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

#include "Config.h"
#include "Python/LoadCim.h"
#include "Python/Component.h"

#include "Components.h"
#include "CIM/Reader.h"

using namespace DPsim;

const char* Python::DocLoadCim =
"load_cim(filenames, frequency=50.0)\n"
"Load a network from CIM file(s).\n"
"\n"
":param filenames: Either a filename or a list of filenames of CIM files to be loaded.\n"
":param frequency: Nominal system frequency in Hz.\n"
":returns: A list of `dpsim.Component`.\n"
"\n"
"Note that in order for the CIM parser to function properly, the CSV "
"files containing the alias configuration have to be in the working directory "
"of the program.\n";
PyObject* Python::LoadCim(PyObject* self, PyObject* args) {
#ifdef WITH_CIM
	double frequency = 50;
	PyObject *list;
	PyBytesObject *filename;
	Logger log("cim.log");
	CIM::Reader *reader;

	if (PyArg_ParseTuple(args, "O&|d", PyUnicode_FSConverter, &filename, &frequency)) {
		reader = new CIM::Reader(2*PI*frequency, log);
		reader->addFile(PyBytes_AsString((PyObject*) filename));
		Py_DECREF(filename);
	}
	else if (PyArg_ParseTuple(args, "O|d", &list, &frequency)) {
		PyErr_Clear();

		if (!PyList_Check(list)) {
			PyErr_SetString(PyExc_TypeError, "First argument must be filename or list of filenames");
			return nullptr;
		}

		reader = new CIM::Reader(2*PI*frequency, log);
		for (int i = 0; i < PyList_Size(list); i++) {
			if (!PyUnicode_FSConverter(PyList_GetItem(list, i), &filename)) {
				delete reader;
				PyErr_SetString(PyExc_TypeError, "First argument must be filename or list of filenames");
				return nullptr;
			}
			reader->addFile(PyBytes_AsString((PyObject*) filename));
			Py_DECREF(filename);
		}
	}
	else {
		PyErr_SetString(PyExc_TypeError, "First argument must be filename or list of filenames");
		return nullptr;
	}

	reader->parseFiles();
	BaseComponent::List comps = reader->getComponents();
	list = PyList_New(comps.size());

	for (unsigned i = 0; i < comps.size(); i++) {
		Python::Component* pyComp = PyObject_New(Component, &Python::ComponentType);
		PyObject_Init((PyObject*) pyComp, &Python::ComponentType);

		pyComp->comp = comps[i];
		PyList_SET_ITEM(list, i, (PyObject*) pyComp);
	}

	delete reader;

	return list;
#else
	PyErr_SetString(PyExc_NotImplementedError, "not implemented on this platform");
	return nullptr;
#endif
}