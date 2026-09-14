#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "../engine/RecordedEvent.h"

class InputRecorder
{
private:
	const std::string m_FileSignature = "MC2-HD-Recording";
	std::string m_FilePath;
	std::map<uint16_t, RecordedEvent*>* m_InputEvents;

	RecordedEventPlayer* EnsurePlayer(uint16_t level, uint16_t playerIdx);
	bool ParseRecording(const std::vector<uint8_t>& data, bool withSpells);

public:
	bool m_IsRecording = false;
	bool m_IsPlaying = false;
	// Which layout the loaded file had.  remc2 later added a per-player spell block but kept
	// the signature, so the two layouts can only be told apart by parsing the whole file.
	bool m_HasSpells = false;
	std::string m_LoadError;

	InputRecorder(const char* filePath);
	~InputRecorder();

	void StartRecording();
	bool StopRecording();
	void PauseRecording(bool pause);
	void ClearInputEvents();

	bool StartPlayback();
	void StopPlayback();

	RecordedEventPlayer* GetCurrentPlayer(int level, int playerIdx);
	RecordedEventTurn* GetCurrentPlayerActions(int level, int playerIdx, int turn);
	void RecordPlayerActions(uint16_t level, uint16_t playerIdx, uint32_t turn, uint64_t sizeBytes, uint8_t* buffer);
	void RecordPlayerSpells(int level, int playerIdx, int16_t* spellsEnabled, uint8_t* spellIndexes, uint8_t* spellLevels, int32_t* spellsExperience);

	bool SaveRecordingToFile(const char* outputFileName);
	bool LoadRecordingFile(const char* inputFileName);

	// One line for a log: format, levels, and turns per player.
	std::string Describe();
};
