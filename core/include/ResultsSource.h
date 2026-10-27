#pragma once
#include "SimulationResults.h"

namespace stosis
{
	// Schlankes Interface fuer alles das eine Zeit groesse ergebnismatrix hat und anzeigbar sein soll: 
	// aktuell CInletUnit und COutletUnit, spaeter auch weitere Einheiten(Crusher, Agglomerator,...). 
	// Der Kurvenvergleich (und spaeter auch andere Anzeigefenster) reden nur mit diesem Interface, 
	// nicht mit den konkreten Klassen - so kann eine Auswahlliste beliebige Einheiten gleich 
	// behandeln, ohne ihren genauen Typ zu kennen.
	class IResultsSource
	{
	public:
		virtual ~IResultsSource() = default;
		virtual const CSimulationResults& GetResults() const = 0;
	};
}