#pragma once

struct high_score_struct
{
	char Name[32];
	int Score;
};

struct high_score_entry
{	
	high_score_struct Entry;
	int Position;
};


enum class Tables;

class high_score
{
public:
	static high_score_struct highscore_table[5];

	static int read();
	static int write();
	// The best score a table saved, as read would find it, or -999 when it has none
	static int BestScore(Tables table);
	static int get_score_position(int score);	

	static void show_high_score_dialog();
	static void show_and_set_high_score_dialog(high_score_entry score);
	static void RenderHighScoreDialog();
	static void clear_table();
	// The next score waiting for a name, taken from the queue the dialog reads; false when none is left. Space Cadet
	// 2026 asks for the names itself.
	static bool TakeNextEntry(high_score_entry& entry);
	// Drops the scores waiting for a name
	static void ClearQueue();
	static void place_new_score_into(high_score_entry data);

private:
	static bool dlg_enter_name;
	static high_score_entry DlgData;
	static bool ShowDialog;
	static std::vector<high_score_entry> ScoreQueue;
};
