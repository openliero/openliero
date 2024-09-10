#pragma once

#include <gvl/io2/stream.hpp>
#include <gvl/serialization/context.hpp>
#include <gvl/crypt/gash.hpp>
#include "mixer/player.hpp"
#include <cstring>
#include <map>
#include <memory>
#include "worm.hpp"
#include "common.hpp"
#include "version.hpp"

struct Game;

struct GameSerializationContext : gvl::serialization_context<GameSerializationContext>
{
	GameSerializationContext()
	: game(0)
	, replayVersion(myReplayVersion)
	{
	}

	struct WormData
	{
		WormData()
		: settingsExpired(true)
		{
		}

		gvl::gash::value_type lastSettingsHash;
		bool settingsExpired;
	};

	int version()
	{
		return replayVersion;
	}

	typedef std::map<Worm*, WormData> WormDataMap;

	Game* game;
	WormDataMap wormData;
	int replayVersion;
};

struct Replay
{
	Replay()
	{
	}

	GameSerializationContext context;

};

struct ReplayWriter : Replay
{
	ReplayWriter(gvl::sink str_init);
	~ReplayWriter();

	void unfocus();
	void focus();

	//gvl::filter_ptr str;
	//gvl::octet_stream_writer writer;
	gvl::octet_writer writer;
	gvl::gash::value_type lastSettingsHash;
	bool settingsExpired;

	void beginRecord(Game& game);
	void recordFrame();
private:
	void endRecord();
};

struct Renderer;

struct ReplayReader : Replay
{
	ReplayReader(gvl::source str_init);

	void unfocus()
	{
		// Nothing
	}

	void focus()
	{
		// Nothing
	}

	std::unique_ptr<Game> beginPlayback(std::shared_ptr<Common> common, std::shared_ptr<SoundPlayer> soundPlayer);
	bool playbackFrame(Renderer& renderer);

	gvl::octet_reader reader;
};
