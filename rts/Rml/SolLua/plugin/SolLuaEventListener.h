/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/*
 * This source file is derived from the code
 * at https://github.com/LoneBoco/RmlSolLua
 * which is under the following license:
 *
 * MIT License
 *
 * Copyright (c) 2022 John Norman
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#pragma once

#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/EventListener.h>
#include <sol2/sol.hpp>


namespace Rml
{
	class Element;
	class ElementDocument;
}

namespace Rml::SolLua
{
	class SolLuaEventListener : public ::Rml::EventListener
			{
	public:
		SolLuaEventListener(sol::state_view &lua, const Rml::String &code, Rml::Element *element);
		SolLuaEventListener(sol::protected_function func, Rml::Element *element);

		void OnDetach(Rml::Element *element) override;
		void ProcessEvent(Rml::Event &event) override;

	private:
		sol::protected_function m_func;
		Rml::Element *m_element;
	};

} // namespace Rml::SolLua
