#pragma once

#include <serialization/context.hpp>
#include <xxhash.h>
#include "io/deflate.hpp"
#include "io/stream.hpp"
#include "mixer/player.hpp"
#include <cstring>
#include <map>
#include <memory>
#include "worm.hpp"
#include "common.hpp"
#include "version.hpp"

struct Game;

struct GameSerializationContext : ser::serialization_context<GameSerializationContext>
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

		uint64_t lastSettingsHash;
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
	ReplayWriter(std::unique_ptr<io::Writer> sink);
	~ReplayWriter();

	void unfocus();
	void focus();

	io::DeflateWriter writer;
	uint64_t lastSettingsHash;
	bool settingsExpired;

	void beginRecord(Game& game);
	void recordFrame();
private:
	void endRecord();
};

struct Renderer;

struct ReplayReader : Replay
{
	ReplayReader(std::unique_ptr<io::Reader> source);

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

	// The full inflated replay is held in memory so we can rewind to
	// the recorded initial position when the user presses R.
	std::vector<uint8_t> data;
	io::MemReader reader;
};
