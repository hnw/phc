/*
 * phc -- the open source PHP compiler
 * See doc/license/README.license for licensing information
 *
 * Conditional constant propagation.
 *
 * In theory this is Wegbreits conditional constant propagation, but I'm just
 * going to steal the implementation from SWPA_lattice, so its a step forward, and then
 * one back, which will probably land in about the same place.
 *
 */

#include "WPA_lattice.h"

using namespace std;
using namespace boost;
using namespace MIR;

WPA_lattice::WPA_lattice (Whole_program* wp)
: WPA (wp)
{
}

void
WPA_lattice::dump(Basic_block* bb)
{
	ins.dump (bb, "IN");
	outs.dump (bb, "OUT");
}

/*
 * Traditionally, we would use 4 sets:
 *
 *		IN, OUT, GEN, KILL
 *	where
 *		OUT = (IN / KILL) U GEN
 *
 *	But its easier to just have IN and OUT. On BB entry, IN is created from the
 *	previous blocks, and copied to OUT. Then OUT is operated on directly.
 *
 *	To make this monotonic, we need to record the old OUT, and compare it during
 *	aggregate_results (which would traditionally perform the equation).
 *
 *
 * Pioli's implementation differs slightly.
 *
 *	So, we need to mark when a solution changes. Pioli's solution was to push
 *	results into the next section, instead of pulling. This meant saving half
 *	the result sets, and setting a block to be executed when a merge changed
 *	the solution. But this might break with branches:
 *
 *		if (...)
 *		1:	$x = 5;
 *		else
 *		2:	;
 *		3:
 *
 *	At 3: we expect $x to have the value BOTTOM, not 5, as it is uninit (and
 *	therefore NULL) at 2:.
 *
 *	This is possible using pull. To make it possible using a push model, we
 *	would need to count the number of times a value is pushed. If it is pushed
 *	less than the number of predecessor edges, it should be merged with NULL.
 *	But would this work for aliasing?
 *
 *	And if we're doing a monotonic analysis, wont running it when NULL has not
 *	yet been pushed ruin the results?  I guess when pushing the results, we
 *	must also push for missing variables.
 *
 *	Pioli avoided all this by doing flow-insensitive analysis. I'm sure its
 *	doable, but not a priority.
 */

void
WPA_lattice::kill_value (Basic_block* bb, Alias_name lhs)
{
	outs[bb->ID][lhs.str()] = TOP;
}


void
WPA_lattice::assign_unknown (Basic_block* bb, Alias_name lhs, certainty cert)
{
	// Dont need to worry about CERT.
	outs[bb->ID][lhs.str()] = BOTTOM;
}


void
WPA_lattice::assign_value (Basic_block* bb, Alias_name lhs, Alias_name rhs, certainty cert)
{
	if (cert != DEFINITE)
		phc_TODO ();

	Lattice_map& lat = outs[bb->ID];
	lat[lhs.str()] = meet (lat[lhs.str()], ins[bb->ID][rhs.str()]);
}


void
WPA_lattice::pull_results (Basic_block* bb)
{
	// TODO: only pull along executable edges
	// TODO: I could imagine this causing error in the presence of recursion.
	changed_flags[bb->ID] = false;

	// Throw away old values (change is detected in aggregation)
	ins[bb->ID].clear ();

	foreach (Basic_block* pred, *bb->get_predecessors ())
		ins[bb->ID].merge (&outs[pred->ID]);

	if (bb->get_predecessors()->size() > 1)
		phc_TODO (); // check if they're executable


	// Copy it straight to outs
	outs[bb->ID].clear ();
	outs[bb->ID].merge (&ins[bb->ID]);
}

void
WPA_lattice::aggregate_results (Basic_block* bb)
{
	// Set solution_changed
	changed_flags[bb->ID] = !outs[bb->ID].equals (&clones[bb->ID]);

	// We probably dont need a full set of clones.
	clones[bb->ID].clear();
	clones[bb->ID].merge (&outs[bb->ID]);
}


void
WPA_lattice::forward_bind (Basic_block* context, CFG* callee_cfg,
										MIR::Actual_parameter_list* actuals,
										MIR::VARIABLE_NAME* retval)
{
	if (context == NULL)
		return;

	// TODO: do we really want to clear? does that make it non-monotonic?
	// TODO: do we need to use the same context as the last time we called from
	// this callsite?
	// TODO: we should have a fresh context anyway.

	int caller = context->ID;
	int callee = callee_cfg->get_entry_bb ()->ID;
	ins[callee].merge(&ins[caller]);
}

void
WPA_lattice::backward_bind (Basic_block* context, CFG* callee_cfg)
{
	if (context == NULL)
		return;

	// TODO: remove variables in the current scope
	//
	int caller = context->ID;
	int callee = callee_cfg->get_entry_bb ()->ID;
	outs[caller].merge(&outs[callee]);
}
