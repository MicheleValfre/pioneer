// Copyright © 2008-2019 Pioneer Developers. See AUTHORS.txt for details
// Licensed under the terms of the GPL v3. See licenses/GPL-3.txt

#include "Container.h"
#include "Context.h"
#include "graphics/Renderer.h"
#include "matrix4x4.h"

namespace UI {

	Container::~Container()
	{
		for (std::vector<RefCountedPtr<Widget>>::iterator i = m_widgets.begin(); i != m_widgets.end(); ++i)
			(*i)->Detach();
	}

	void Container::Update()
	{
		// widgets can add/remove other widgets during Update. that screws up the
		// iterators when traversing the widget list.  rather than try and detect
		// it, we just take a copy of the list
		std::vector<RefCountedPtr<Widget>> widgets = m_widgets;
		for (std::vector<RefCountedPtr<Widget>>::iterator i = widgets.begin(); i != widgets.end(); ++i)
			(*i)->Update();
	}

	void Container::Draw()
	{
		if(m_hidden) return;
		Context *c = GetContext();

		for (auto end = m_widgets.end(), it = m_widgets.begin(); it != end; ++it)
			c->DrawWidget((*it).Get());
	}

	void Container::LayoutChildren()
	{
		for (auto end = m_widgets.end(), it = m_widgets.begin(); it != end; ++it)
			(*it)->Layout();
	}

	void Container::AddWidget(Widget *widget)
	{
		if (widget->GetContainer())
			widget->GetContainer()->RemoveWidget(widget);

		std::vector<RefCountedPtr<Widget>>::iterator i;
		for (i = m_widgets.begin(); i != m_widgets.end(); ++i)
			if ((*i).Get() == widget) break;
		assert(i == m_widgets.end());

		widget->Attach(this);
		m_widgets.push_back(RefCountedPtr<Widget>(widget));

		GetContext()->RequestLayout();
	}

	void Container::RemoveWidget(Widget *widget)
	{
		assert(widget->GetContainer() == this);

		std::vector<RefCountedPtr<Widget>>::iterator i;
		for (i = m_widgets.begin(); i != m_widgets.end(); ++i)
			if ((*i).Get() == widget) break;
		if (i == m_widgets.end())
			return;

		widget->Detach();
		m_widgets.erase(i);

		GetContext()->RequestLayout();
	}

	void Container::RemoveAllWidgets()
	{
		for (auto end = m_widgets.end(), it = m_widgets.begin(); it != end; ++it)
			(*it)->Detach();
		m_widgets.clear();
		GetContext()->RequestLayout();
	}

	void Container::Disable()
	{
		DisableChildren();
		Widget::Disable();
	}

	void Container::Enable()
	{
		EnableChildren();
		Widget::Enable();
	}

	void Container::NotifyVisible(bool visible)
	{
		const bool wasVisible = IsVisible();
		Widget::NotifyVisible(visible);
		if (wasVisible != visible) {
			for (std::vector<RefCountedPtr<Widget>>::iterator i = m_widgets.begin(); i != m_widgets.end(); ++i) {
				Widget *w = (*i).Get();
				w->NotifyVisible(visible);
			}
		}
	}

	void Container::DisableChildren()
	{
		for (std::vector<RefCountedPtr<Widget>>::iterator i = m_widgets.begin(); i != m_widgets.end(); ++i) {
			Widget *w = (*i).Get();
			w->SetDisabled(true);
			if (w->IsContainer()) {
				static_cast<Container *>(w)->DisableChildren();
			}
		}
	}

	void Container::EnableChildren()
	{
		for (std::vector<RefCountedPtr<Widget>>::iterator i = m_widgets.begin(); i != m_widgets.end(); ++i) {
			Widget *w = (*i).Get();
			w->SetDisabled(false);
			if (w->IsContainer()) {
				static_cast<Container *>(w)->EnableChildren();
			}
		}
	}

	void Container::SetWidgetDimensions(Widget *widget, const Point &position, const Point &size)
	{
		assert(widget->GetContainer() == this);

		widget->SetDimensions(position, size);
	}

	Widget *Container::GetWidgetAt(const Point &pos)
	{
		if (!Contains(pos)) return 0;

		for (auto end = m_widgets.rend(), it = m_widgets.rbegin(); it != end; ++it) {
			const auto widget = *it;
			const Point relpos = pos - widget->GetPosition() - widget->GetDrawOffset();
			if (widget->IsContainer()) {
				Widget *w = static_cast<Container *>(widget.Get())->GetWidgetAt(relpos);
				if (w) return w;
			} else if (widget->Contains(relpos))
				return widget.Get();
		}

		return this;
	}

	void Container::CollectShortcuts(std::map<KeySym, Widget *> &shortcuts)
	{
		{
			const std::set<KeySym> &widgetShortcuts = GetShortcuts();
			if (!widgetShortcuts.empty())
				for (std::set<KeySym>::const_iterator j = widgetShortcuts.begin(); j != widgetShortcuts.end(); ++j)
					shortcuts[*j] = this;
		}

		for (auto end = m_widgets.rend(), it = m_widgets.rbegin(); it != end; ++it) {
			const auto widget = *it;
			if (widget->IsContainer())
				static_cast<Container *>(widget.Get())->CollectShortcuts(shortcuts);
			else {
				const std::set<KeySym> &widgetShortcuts = widget->GetShortcuts();
				if (!widgetShortcuts.empty())
					for (std::set<KeySym>::const_iterator j = widgetShortcuts.begin(); j != widgetShortcuts.end(); ++j)
						shortcuts[*j] = widget.Get();
			}
		}
	}

} // namespace UI
