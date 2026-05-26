#include "booleanSwitchBehavior.hpp"

#include "menu.hpp"
#include "menuItem.hpp"
#include "../sfx.hpp"
#include "../common.hpp"

bool BooleanSwitchBehavior::onLeftRight(Menu& menu, MenuItem& item, int dir)
{
	if(dir > 0)
		sfx.play(common, common.soundHook[SoundMenuMoveUp]);
	else
		sfx.play(common, common.soundHook[SoundMenuMoveDown]);

	set(!v);
	onUpdate(menu, item);
	return false;
}

int BooleanSwitchBehavior::onEnter(Menu& menu, MenuItem& item)
{
	sfx.play(common, common.soundHook[SoundMenuSelect]);
	set(!v);
	onUpdate(menu, item);
	return -1;
}

void BooleanSwitchBehavior::onUpdate(Menu& menu, MenuItem& item)
{
	item.value = common.texts.onoff[v];
	item.hasValue = true;
}
