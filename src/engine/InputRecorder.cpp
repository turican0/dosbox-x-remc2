#include "InputRecorder.h"
#include <cstdio>

using namespace std;

// Layout of a recording, as remc2 writes it (little endian):
//
//   "MC2-HD-RecordV03"                          16 bytes
//   per level:
//     uint16 Level, uint16 PlayerCount
//     uint32 SaveCount, per save: uint32 Size, Size bytes (level start save, loaded as in remc2)
//     per player:
//       uint16 PlayerIdx, uint32 TurnCount
//       int16[26] SpellsEnabled, uint8[26] SpellIndexes,
//       uint8[26] SpellLevels, int32[26] SpellsExperience       (208 bytes)
//       per turn: uint32 Turn, uint32 Rand, uint32 Rand, uint32 SizeBytes, SizeBytes bytes of input

namespace
{
	const int kSpellCount = 26;
	const size_t kSpellBlockSize = kSpellCount * (2 + 1 + 1 + 4);
	const uint16_t kMaxPlayers = 8;
	const uint32_t kMaxTurnBytes = 64;

	struct Reader
	{
		const std::vector<uint8_t>& d;
		size_t p;
		bool has(size_t n) const { return p + n <= d.size(); }
		uint16_t u16() { uint16_t v = (uint16_t)(d[p] | (d[p + 1] << 8)); p += 2; return v; }
		uint32_t u32()
		{
			uint32_t v = (uint32_t)d[p] | ((uint32_t)d[p + 1] << 8) | ((uint32_t)d[p + 2] << 16) | ((uint32_t)d[p + 3] << 24);
			p += 4;
			return v;
		}
	};
}

InputRecorder::InputRecorder(const char* filePath)
{
	m_FilePath = filePath;
	m_InputEvents = new std::map<uint16_t, RecordedEvent*>();
}

InputRecorder::~InputRecorder()
{
	ClearInputEvents();
	delete m_InputEvents;
}

void InputRecorder::StartRecording()
{
	m_IsRecording = true;
}

void InputRecorder::ClearInputEvents()
{
	for (auto& level : *m_InputEvents)
	{
		for (auto& player : *level.second->Players)
		{
			for (auto& turn : *player.second->Turns)
				delete turn.second;
			delete player.second->Turns;
			delete player.second;
		}
		delete level.second->Players;
		delete level.second->Header;
		delete level.second;
	}
	m_InputEvents->clear();
}

bool InputRecorder::StopRecording()
{
	m_IsRecording = false;
	if (SaveRecordingToFile(m_FilePath.c_str()))
	{
		ClearInputEvents();
		return true;
	}
	return false;
}

void InputRecorder::PauseRecording(bool pause)
{
	m_IsRecording = !pause;
	m_IsPlaying = !pause;
}

bool InputRecorder::StartPlayback()
{
	if (LoadRecordingFile(m_FilePath.c_str()))
		m_IsPlaying = true;

	return m_IsPlaying;
}

void InputRecorder::StopPlayback()
{
	m_IsPlaying = false;
}

RecordedEventPlayer* InputRecorder::GetCurrentPlayer(int level, int playerIdx)
{
	if (!m_IsPlaying || m_InputEvents->count(level) == 0 || m_InputEvents->at(level)->Players->count(playerIdx) == 0)
		return nullptr;

	return m_InputEvents->at(level)->Players->at(playerIdx);
}

void InputRecorder::RecordLevelSave(uint16_t level, std::vector<uint8_t> save)
{
	if (m_IsRecording)
		EnsureLevel(level)->Saves.push_back(std::move(save));
}

bool InputRecorder::SaveRecording()
{
	return SaveRecordingToFile(m_FilePath.c_str());
}

void InputRecorder::LevelStarted(uint16_t level)
{
	m_LevelStarts[level]++;
}

const std::vector<uint8_t>* InputRecorder::GetLevelSave(int level)
{
	const size_t start = m_LevelStarts[(uint16_t)level];
	if (m_InputEvents->count(level) == 0 || start == 0 || m_InputEvents->at(level)->Saves.size() < start)
		return nullptr;
	return &m_InputEvents->at(level)->Saves[start - 1];
}

RecordedEventTurn* InputRecorder::GetCurrentPlayerActions(int level, int playerIdx, int turn)
{
	RecordedEventPlayer* player = GetCurrentPlayer(level, playerIdx);
	if (player == nullptr || player->Turns->count(turn) == 0)
		return nullptr;

	return player->Turns->at(turn);
}

RecordedEvent* InputRecorder::EnsureLevel(uint16_t level)
{
	if (m_InputEvents->count(level) == 0)
	{
		RecordedEvent* event = new RecordedEvent();
		event->Header = new RecordedEventHeader();
		event->Header->Level = level;
		event->Players = new std::map<uint16_t, RecordedEventPlayer*>();
		m_InputEvents->insert({ level, event });
	}
	return m_InputEvents->at(level);
}

RecordedEventPlayer* InputRecorder::EnsurePlayer(uint16_t level, uint16_t playerIdx)
{
	RecordedEvent* event = EnsureLevel(level);
	if (event->Players->count(playerIdx) == 0)
	{
		RecordedEventPlayer* player = new RecordedEventPlayer();
		player->PlayerIdx = playerIdx;
		player->Turns = new std::map<uint32_t, RecordedEventTurn*>();
		event->Players->insert({ playerIdx, player });
		event->Header->PlayerCount = (uint16_t)event->Players->size();
	}
	return event->Players->at(playerIdx);
}

void InputRecorder::RecordPlayerSpells(int level, int playerIdx, int16_t* spellsEnabled, uint8_t* spellIndexes, uint8_t* spellLevels, int32_t* spellsExperience)
{
	if (!m_IsRecording)
		return;

	RecordedEventPlayer* player = EnsurePlayer((uint16_t)level, (uint16_t)playerIdx);
	if (player->SpellsEnabled == nullptr)
	{
		player->SpellsEnabled = new int16_t[kSpellCount];
		player->SpellIndexes = new uint8_t[kSpellCount];
		player->SpellLevels = new uint8_t[kSpellCount];
		player->SpellsExperience = new int32_t[kSpellCount];
	}
	for (int i = 0; i < kSpellCount; i++)
	{
		player->SpellsEnabled[i] = spellsEnabled[i];
		player->SpellIndexes[i] = spellIndexes[i];
		player->SpellLevels[i] = spellLevels[i];
		player->SpellsExperience[i] = spellsExperience[i];
	}
}

void InputRecorder::RecordPlayerActions(uint16_t level, uint16_t playerIdx, uint32_t turn, uint32_t rand, uint64_t sizeBytes, uint8_t* buffer)
{
	if (!m_IsRecording)
		return;

	RecordedEventPlayer* player = EnsurePlayer(level, playerIdx);
	RecordedEventTurn* recorded;
	if (player->Turns->count(turn) == 0)
	{
		recorded = new RecordedEventTurn();
		player->Turns->insert({ turn, recorded });
	}
	else
	{
		recorded = player->Turns->at(turn);
		delete[] recorded->Bytes;
	}
	recorded->Turn = turn;
	recorded->Rand = rand;
	recorded->SizeBytes = (uint32_t)sizeBytes;
	recorded->Bytes = new uint8_t[(size_t)sizeBytes];
	memcpy(recorded->Bytes, buffer, (size_t)sizeBytes);
	player->TurnCount = (uint32_t)player->Turns->size();
}

bool InputRecorder::SaveRecordingToFile(const char* outputFileName)
{
	if (m_InputEvents == nullptr || m_InputEvents->empty())
		return false;

	FILE* eventsFile = fopen(outputFileName, "wb");
	if (!eventsFile)
		return false;

	// players without spells get zeros
	fwrite(m_FileSignature.c_str(), m_FileSignature.length(), 1, eventsFile);
	const int16_t zero16[kSpellCount] = { 0 };
	const uint8_t zero8[kSpellCount] = { 0 };
	const int32_t zero32[kSpellCount] = { 0 };

	for (auto& level : *m_InputEvents)
	{
		uint16_t levelNumber = level.first;
		uint16_t playerCount = (uint16_t)level.second->Players->size();
		fwrite(&levelNumber, sizeof(levelNumber), 1, eventsFile);
		fwrite(&playerCount, sizeof(playerCount), 1, eventsFile);
		const uint32_t saveCount = (uint32_t)level.second->Saves.size();
		fwrite(&saveCount, sizeof(saveCount), 1, eventsFile);
		for (const auto& save : level.second->Saves)
		{
			const uint32_t saveSize = (uint32_t)save.size();
			fwrite(&saveSize, sizeof(saveSize), 1, eventsFile);
			fwrite(save.data(), 1, saveSize, eventsFile);
		}

		for (auto& playerIt : *level.second->Players)
		{
			RecordedEventPlayer* player = playerIt.second;
			uint16_t playerIndex = playerIt.first;
			uint32_t turnCount = (uint32_t)player->Turns->size();
			fwrite(&playerIndex, sizeof(playerIndex), 1, eventsFile);
			fwrite(&turnCount, sizeof(turnCount), 1, eventsFile);
			fwrite(player->SpellsEnabled ? player->SpellsEnabled : zero16, sizeof(int16_t), kSpellCount, eventsFile);
			fwrite(player->SpellIndexes ? player->SpellIndexes : zero8, sizeof(uint8_t), kSpellCount, eventsFile);
			fwrite(player->SpellLevels ? player->SpellLevels : zero8, sizeof(uint8_t), kSpellCount, eventsFile);
			fwrite(player->SpellsExperience ? player->SpellsExperience : zero32, sizeof(int32_t), kSpellCount, eventsFile);

			for (auto& turnIt : *player->Turns)
			{
				RecordedEventTurn* turn = turnIt.second;
				fwrite(&turn->Turn, sizeof(turn->Turn), 1, eventsFile);
				fwrite(&turn->Rand, sizeof(turn->Rand), 1, eventsFile);
				fwrite(&turn->Rand, sizeof(turn->Rand), 1, eventsFile);
				fwrite(&turn->SizeBytes, sizeof(turn->SizeBytes), 1, eventsFile);
				fwrite(turn->Bytes, turn->SizeBytes, 1, eventsFile);
			}
		}
	}
	return fclose(eventsFile) == 0;
}

bool InputRecorder::ParseRecording(const std::vector<uint8_t>& data)
{
	ClearInputEvents();
	Reader r{ data, m_FileSignature.length() };

	while (r.p < data.size())
	{
		if (!r.has(4)) { m_LoadError = "truncated level header"; break; }
		uint16_t level = r.u16();
		uint16_t playerCount = r.u16();
		if (playerCount > kMaxPlayers) { m_LoadError = "player count out of range"; break; }
		if (!r.has(4)) { m_LoadError = "truncated save count"; break; }
		uint32_t saveCount = r.u32();
		bool ok = true;
		while (saveCount-- && ok)
		{
			if (!r.has(4)) { m_LoadError = "truncated save"; ok = false; break; }
			const uint32_t saveSize = r.u32();
			if (!r.has(saveSize)) { m_LoadError = "truncated save"; ok = false; break; }
			EnsureLevel(level)->Saves.emplace_back(data.begin() + r.p, data.begin() + r.p + saveSize);
			r.p += saveSize;
		}
		for (uint16_t k = 0; k < playerCount && ok; k++)
		{
			if (!r.has(6 + kSpellBlockSize)) { m_LoadError = "truncated player header"; ok = false; break; }
			uint16_t playerIdx = r.u16();
			uint32_t turnCount = r.u32();
			if (playerIdx >= kMaxPlayers) { m_LoadError = "player index out of range"; ok = false; break; }

			// A player that is already known keeps its spells; its turns are merged in, the
			// same as remc2 does when a file holds a level twice.
			const bool known = m_InputEvents->count(level) != 0 && m_InputEvents->at(level)->Players->count(playerIdx) != 0;
			RecordedEventPlayer* player = EnsurePlayer(level, playerIdx);
			int16_t enabled[kSpellCount];
			uint8_t indexes[kSpellCount];
			uint8_t levels[kSpellCount];
			int32_t experience[kSpellCount];
			for (int i = 0; i < kSpellCount; i++) enabled[i] = (int16_t)r.u16();
			for (int i = 0; i < kSpellCount; i++) indexes[i] = data[r.p++];
			for (int i = 0; i < kSpellCount; i++) levels[i] = data[r.p++];
			for (int i = 0; i < kSpellCount; i++) experience[i] = (int32_t)r.u32();
			if (!known)
			{
				player->SpellsEnabled = new int16_t[kSpellCount];
				player->SpellIndexes = new uint8_t[kSpellCount];
				player->SpellLevels = new uint8_t[kSpellCount];
				player->SpellsExperience = new int32_t[kSpellCount];
				memcpy(player->SpellsEnabled, enabled, sizeof(enabled));
				memcpy(player->SpellIndexes, indexes, sizeof(indexes));
				memcpy(player->SpellLevels, levels, sizeof(levels));
				memcpy(player->SpellsExperience, experience, sizeof(experience));
			}

			for (uint32_t t = 0; t < turnCount; t++)
			{
				if (!r.has(16)) { m_LoadError = "truncated turn header"; ok = false; break; }
				RecordedEventTurn* turn = new RecordedEventTurn();
				turn->Turn = r.u32();
				turn->Rand = r.u32();
				r.u32();
				turn->SizeBytes = r.u32();
				if (turn->SizeBytes == 0 || turn->SizeBytes > kMaxTurnBytes || !r.has(turn->SizeBytes))
				{
					m_LoadError = "turn size out of range";
					delete turn;
					ok = false;
					break;
				}
				turn->Bytes = new uint8_t[turn->SizeBytes];
				memcpy(turn->Bytes, &data[r.p], turn->SizeBytes);
				r.p += turn->SizeBytes;
				if (player->Turns->count(turn->Turn) != 0)
				{
					delete player->Turns->at(turn->Turn);
					player->Turns->erase(turn->Turn);
				}
				player->Turns->insert({ turn->Turn, turn });
			}
			player->TurnCount = (uint32_t)player->Turns->size();
		}
		if (!ok)
			break;
	}

	if (r.p != data.size() || !m_LoadError.empty())
	{
		if (m_LoadError.empty())
			m_LoadError = "trailing data";
		ClearInputEvents();
		return false;
	}
	return true;
}

bool InputRecorder::LoadRecordingFile(const char* inputFileName)
{
	m_LoadError.clear();
	FILE* eventsFile = fopen(inputFileName, "rb");
	if (eventsFile == nullptr)
	{
		m_LoadError = "cannot open the file";
		return false;
	}
	std::vector<uint8_t> data;
	uint8_t chunk[65536];
	size_t n;
	while ((n = fread(chunk, 1, sizeof(chunk), eventsFile)) > 0)
		data.insert(data.end(), chunk, chunk + n);
	fclose(eventsFile);

	if (data.size() < m_FileSignature.length() || memcmp(data.data(), m_FileSignature.c_str(), m_FileSignature.length()) != 0)
	{
		m_LoadError = "not a recording (signature)";
		return false;
	}

	return ParseRecording(data);
}

std::string InputRecorder::Describe()
{
	std::string s = m_FileSignature;
	char buf[128];
	for (auto& level : *m_InputEvents)
	{
		snprintf(buf, sizeof(buf), "; level %u:", (unsigned)level.first);
		s += buf;
		for (auto& player : *level.second->Players)
		{
			snprintf(buf, sizeof(buf), " p%u=%u", (unsigned)player.first, (unsigned)player.second->Turns->size());
			s += buf;
		}
	}
	return s;
}
