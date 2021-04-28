//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "Progress.h"
#include <iostream>

AtomicProgress::AtomicProgress(bool createState, float totalPercentage) :
	m_numSteps(1),
	m_percentageOfParent(totalPercentage),
	m_stepPercent(m_numSteps == 0 ? totalPercentage : totalPercentage / m_numSteps),
	m_atomicState(createState ? std::make_shared<AtomicPercent32>(0.f) : nullptr)
{

}
//
//AtomicProgress::AtomicProgress(AtomicPercent32 * state, float totalPercentage) :
//	m_numSteps(1),
//	m_percentageOfParent(totalPercentage),
//	m_stepPercent(m_numSteps == 0 ? totalPercentage : totalPercentage / m_numSteps),
//	m_atomicState(state), m_isStateOwner(false)
//{
//
//}

AtomicProgress::AtomicProgress(const AtomicProgress & parent, float percentageOfParent) :
	m_numSteps(1),
	m_percentageOfParent(parent.m_percentageOfParent * percentageOfParent),
	m_stepPercent(m_numSteps == 0 ? m_percentageOfParent : m_percentageOfParent / m_numSteps),
	m_atomicState(parent.m_atomicState)
{

}

void AtomicProgress::resetProgress(float p)
{
	if (!m_atomicState)
		return;

	*m_atomicState = p;
}

float AtomicProgress::progress() const
{
	if (!m_atomicState)
		return -1.f;

	return float(*m_atomicState);
}

void AtomicProgress::setAvailablePercent(float availablePercent)
{
	m_percentageOfParent = availablePercent;
	m_stepPercent = m_numSteps == 0 ? availablePercent : availablePercent / m_numSteps;
}

void AtomicProgress::setNumSteps(int numSteps)
{
	m_numSteps = numSteps;
	m_stepPercent = m_numSteps == 0 ? m_percentageOfParent : m_percentageOfParent / m_numSteps;
}

AtomicProgress& AtomicProgress::operator+=(int steps)
{
	if (!m_atomicState)
		return *this;

	*m_atomicState += steps * m_stepPercent;

	return *this;
}