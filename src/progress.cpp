//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "progress.h"
#include <iostream>

AtomicProgress::AtomicProgress(bool createState, float totalPercentage) :
	m_num_steps(1),
	m_percentage_of_parent(totalPercentage),
	m_step_percent(m_num_steps == 0 ? totalPercentage : totalPercentage / m_num_steps),
	m_state(createState ? std::make_shared<State>() : nullptr)
{

}
//
//AtomicProgress::AtomicProgress(AtomicPercent32 * state, float totalPercentage) :
//	m_num_steps(1),
//	m_percentage_of_parent(totalPercentage),
//	m_step_percent(m_num_steps == 0 ? totalPercentage : totalPercentage / m_num_steps),
//	m_state(state), m_isStateOwner(false)
//{
//
//}

AtomicProgress::AtomicProgress(const AtomicProgress & parent, float percentageOfParent) :
	m_num_steps(1),
	m_percentage_of_parent(parent.m_percentage_of_parent * percentageOfParent),
	m_step_percent(m_num_steps == 0 ? m_percentage_of_parent : m_percentage_of_parent / m_num_steps),
	m_state(parent.m_state)
{

}

void AtomicProgress::reset_progress(float p)
{
	if (!m_state)
		return;

	m_state->progress = p;
}

float AtomicProgress::progress() const
{
	return m_state ? float(m_state->progress) : -1.f;
}

void AtomicProgress::set_available_percent(float available_percent)
{
	m_percentage_of_parent = available_percent;
	m_step_percent = m_num_steps == 0 ? available_percent : available_percent / m_num_steps;
}

void AtomicProgress::set_num_steps(int num_steps)
{
	m_num_steps = num_steps;
	m_step_percent = m_num_steps == 0 ? m_percentage_of_parent : m_percentage_of_parent / m_num_steps;
}

AtomicProgress& AtomicProgress::operator+=(int steps)
{
	if (!m_state)
		return *this;

	m_state->progress += steps * m_step_percent;

	return *this;
}


bool AtomicProgress::canceled() const
{
	return m_state ? m_state->canceled : false;
}

void AtomicProgress::cancel()
{
	if (!m_state)
		return;

	m_state->canceled = true;
}