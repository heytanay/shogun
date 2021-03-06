/*
 * This software is distributed under BSD 3-clause license (see LICENSE file).
 *
 * Authors: Jacob Walker, Roman Votyakov, Heiko Strathmann, Giovanni De Toni,
 *          Sergey Lisitsyn
 */

#include <shogun/evaluation/GradientEvaluation.h>
#include <shogun/evaluation/GradientResult.h>

#include <utility>

using namespace shogun;

GradientEvaluation::GradientEvaluation() : MachineEvaluation()
{
	init();
}

GradientEvaluation::GradientEvaluation(std::shared_ptr<Machine> machine, std::shared_ptr<Features> features,
		std::shared_ptr<Labels> labels, std::shared_ptr<Evaluation> evaluation_crit, bool autolock) :
		MachineEvaluation(std::move(machine), std::move(features), std::move(labels), NULL, std::move(evaluation_crit), autolock)
{
	init();
}

void GradientEvaluation::init()
{
	m_diff=NULL;

	SG_ADD(
	    &m_diff, "differentiable_function", "Differentiable function",
	    ParameterProperties::HYPER);
}

GradientEvaluation::~GradientEvaluation()
{
}

void GradientEvaluation::update_parameter_dictionary() const
{
	m_diff->build_gradient_parameter_dictionary(m_parameter_dictionary);
}

std::shared_ptr<EvaluationResult> GradientEvaluation::evaluate_impl() const
{
	if (parameter_hash_changed())
		update_parameter_dictionary();

	// create gradient result object
	auto result=std::make_shared<GradientResult>();


	// set function value
	result->set_value(m_diff->get_value());

	auto gradient=m_diff->get_gradient(m_parameter_dictionary);

	// set gradient and parameter dictionary
	result->set_gradient(gradient);
	std::map<std::string, std::shared_ptr<SGObject>> param_dictionary;
	for (const auto& p: m_parameter_dictionary)
		param_dictionary.emplace(p.first.first, p.second);	

	result->set_parameter_dictionary(param_dictionary);
	update_parameter_hash();

	return result;
}
