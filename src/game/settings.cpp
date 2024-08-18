#include "settings.hpp"

#include "keys.hpp"
#include "gfx.hpp"
#include "filesystem.hpp"

#include <gvl/io2/fstream.hpp>
#include <gvl/serialization/context.hpp>
#include <gvl/serialization/archive.hpp>
#include <gvl/serialization/toml.hpp>

#include <gvl/crypt/gash.hpp>

int const Settings::wormAnimTab[] =
{
	0,
	7,
	0,
	14
};

Extensions::Extensions()
: recordReplays(true)
, loadPowerlevelPalette(true)
, bloodParticleMax(700)
, aiFrames(70*2), aiMutations(2)
, aiTraces(false)
, aiParallels(3)
, fullscreen(false)
, zoneTimeout(30)
, selectBotWeapons(true)
, allowViewingSpawnPoint(false)
, singleScreenReplay(false)
, spectatorWindow(false)
, tc(std::string("openliero"))
{
}

Settings::Settings()
: maxBonuses(4)
, blood(100)
, timeToLose(600)
, flagsToWin(20)
, gameMode(0)
, shadow(true)
, loadChange(true)
, namesOnBonuses(false)
, regenerateLevel(false)
, lives(15)
, loadingTime(100)
, randomLevel(true)
, map(true)
, screenSync(true)
{
	std::memset(weapTable, 0, sizeof(weapTable));

	wormSettings[0].reset(new WormSettings);
	wormSettings[1].reset(new WormSettings);

	wormSettings[0]->color = 32;
	wormSettings[1]->color = 41;

	unsigned char defControls[2][7] =
	{
		{0x13, 0x21, 0x20, 0x22, 0x1D, 0x2A, 0x38},
		{0xA0, 0xA8, 0xA3, 0xA5, 0x75, 0x90, 0x36}
	};

	unsigned char defRGB[2][3] =
	{
		{26, 26, 63},
		{15, 43, 15}
	};

	for(int i = 0; i < 2; ++i)
	{
		for(int j = 0; j < 7; ++j)
		{
			wormSettings[i]->controls[j] = defControls[i][j];
			wormSettings[i]->controlsEx[j] = defControls[i][j];
		}

		for(int j = 0; j < 3; ++j)
		{
			wormSettings[i]->rgb[j] = defRGB[i][j];
		}
	}
}

typedef gvl::in_archive<gvl::octet_reader> in_archive_t;
typedef gvl::out_archive<gvl::octet_writer> out_archive_t;

bool Settings::load(FsNode node)
{
	try
	{
		auto reader = node.toOctetReader();
		gvl::default_serialization_context context;

		gvl::toml::reader<gvl::octet_reader> ar(reader);

		archive_text(*this, ar);
	}
	catch (std::runtime_error&)
	{
		return false;
	}

	return true;
}

bool Settings::loadLegacy(FsNode node)
{
	try
	{
		auto reader = node.toOctetReader();
		gvl::default_serialization_context context;

		archive_liero(in_archive_t(reader, context), *this);
	}
	catch (std::runtime_error&)
	{
		return false;
	}

	return true;
}

gvl::gash::value_type& Settings::updateHash()
{
	gvl::default_serialization_context context;
	gvl::hash_accumulator<gvl::gash> ha;

	archive(gvl::out_archive<gvl::hash_accumulator<gvl::gash>, gvl::default_serialization_context>(ha, context), *this);

	ha.flush();
	hash = ha.final();
	return hash;
}

void Settings::save(FsNode node)
{
	auto writer = node.toOctetWriter();

	gvl::toml::writer<gvl::octet_writer> ar(writer);

	archive_text(*this, ar);
}
