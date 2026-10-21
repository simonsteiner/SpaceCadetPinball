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


class high_score
{
public:
	static high_score_struct highscore_table[5];

	static int read();
	static int write();
	static int get_score_position(int score);	

	static void show_high_score_dialog();
	static void show_and_set_high_score_dialog(high_score_entry score);
	static void RenderHighScoreDialog();
	static void clear_table();
	// The next score waiting for a name, taken from the queue the dialog reads; false when none is left. Space Cadet
	// 2026 asks for the names itself.
	static bool TakeNextEntry(high_score_entry& entry);
	static void place_new_score_into(high_score_entry data);

private:
	static bool dlg_enter_name;
	static high_score_entry DlgData;
	static bool ShowDialog;
	static std::vector<high_score_entry> ScoreQueue;
};
