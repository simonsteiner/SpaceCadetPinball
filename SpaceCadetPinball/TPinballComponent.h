#pragma once

struct SpriteData;
struct vector2i;
struct zmap_header_type;
struct gdrv_bitmap8;
struct render_sprite;
struct component_control;
struct vector2;
class TPinballTable;


enum class MessageCode
{
	// Private codes <1000, different meaning for each component
	TFlipperNull = 0,
	TFlipperExtend = 1,
	TFlipperRetract = 2,

	TLightTurnOff = 0,
	TLightTurnOn = 1,
	TLightGetLightOnFlag = 2,
	TLightGetFlasherOnFlag = 3,
	TLightFlasherStart = 4,
	TLightApplyMultDelay = 5,
	TLightApplyDelay = 6,
	TLightFlasherStartTimed = 7,
	TLightTurnOffTimed = 8,
	TLightTurnOnTimed = 9,
	TLightSetOnStateBmpIndex = 11,
	TLightIncOnStateBmpIndex = 12,
	TLightDecOnStateBmpIndex = 13,
	TLightResetTimed = 14,
	TLightFlasherStartTimedThenStayOn = 15,
	TLightFlasherStartTimedThenStayOff = 16,
	TLightToggleValue = 17,
	TLightResetAndToggleValue = 18,
	TLightResetAndTurnOn = 19,
	TLightResetAndTurnOff = 20,
	TLightToggle = 21,
	TLightResetAndToggle = 22,
	TLightSetMessageField = 23,
	TLightFtTmpOverrideOn = -24,
	TLightFtTmpOverrideOff = -25,
	TLightFtResetOverride = -26,

	TLightGroupNull = 0,
	TLightGroupStepBackward = 24,
	TLightGroupStepForward = 25,
	TLightGroupAnimationBackward = 26,
	TLightGroupAnimationForward = 27,
	TLightGroupLightShowAnimation = 28,
	TLightGroupGameOverAnimation = 29,
	TLightGroupRandomAnimationSaturation = 30,
	TLightGroupRandomAnimationDesaturation = 31,
	TLightGroupOffsetAnimationForward = 32,
	TLightGroupOffsetAnimationBackward = 33,
	TLightGroupReset = 34,
	TLightGroupTurnOnAtIndex = 35,
	TLightGroupTurnOffAtIndex = 36,
	TLightGroupGetOnCount = 37,
	TLightGroupGetLightCount = 38,
	TLightGroupGetMessage2 = 39,
	TLightGroupGetAnimationFlag = 40,
	TLightGroupResetAndTurnOn = 41,
	TLightGroupResetAndTurnOff = 42,
	TLightGroupRestartNotifyTimer = 43,
	TLightGroupFlashWhenOn = 44,
	TLightGroupToggleSplitIndex = 45,
	TLightGroupStartFlasher = 46,
	TLightGroupCountdownEnded = 47,

	TBumperSetBmpIndex = 11,
	TBumperIncBmpIndex = 12,
	TBumperDecBmpIndex = 13,

	TComponentGroupResetNotifyTimer = 48,

	TPopupTargetDisable = 49,
	TPopupTargetEnable = 50,

	TBlockerDisable = 51,
	TBlockerEnable = 52,
	TBlockerRestartTimeout = 59,

	// TAnimatedBlocker, Full Tilt's bonus tables: CADET.EXE's numbers, so their rules can send them as they are
	TAnimatedBlockerAnimateOff = 55,
	TAnimatedBlockerAnimateOn = 56,
	TAnimatedBlockerSetState = 57,
	TAnimatedBlockerAnimateTo = 58,
	TAnimatedBlockerSetOn = 59,
	TAnimatedBlockerSetOff = 60,
	TAnimatedBlockerRestartTimeout = 69,

	// TDiverter, CADET.EXE's numbers; CADET.EXE also takes 63 and 75 as Enable and 62 and 76 as Disable
	TDiverterDisable = 55,
	TDiverterEnable = 56,

	// TLightSweeps, CADET.EXE's number: start the sweep whose attributes are in state <value> of light_sweeps
	TLightSweepsStart = 27,

	TGateDisable = 53,
	TGateEnable = 54,

	TKickoutRestartTimer = 55,

	// CADET.EXE's numbers, the bonus tables only: TKickout, TKickback and TMagnet switched on and off. TKickout lets
	// go of a ball it holds as it goes off; a TKickback that is off doesn't kick
	TKickoutEnable = 75,
	TKickoutDisable = 76,
	TKickbackEnable = 75,
	TKickbackDisable = 76,
	TMagnetEnable = 75,
	TMagnetDisable = 76,

	TSinkUnknown7 = 7,
	TSinkResetTimer = 56,

	TSoloTargetDisable = 49,
	TSoloTargetEnable = 50,

	TTimerResetTimer = 59,
	// CADET.EXE's number, the bonus tables only: the time left in milliseconds, 0 when the timer isn't running
	TTimerGetTimeLeft = 72,

	// THole, CADET.EXE's number, the bonus tables only: let the ball go after <value> seconds, at once if it is 0 or
	// less
	THoleRelease = 67,

	ControlBallCaptured = 57,
	ControlBallReleased = 58,
	ControlTimerExpired = 60,
	ControlNotifyTimerExpired = 61,
	ControlSpinnerLoopReset = 62,
	ControlCollision = 63,
	ControlEnableMultiplier = 64,
	ControlDisableMultiplier = 65,
	ControlMissionComplete = 66,
	ControlMissionStarted = 67,
	// Full Tilt only (Phase F): the ball crossed a ramp exit (TRampExit) into its second or its first layer.
	// CADET.EXE numbers them 0x56 and 0x55 and its TRamp sends them too, for walls 1302 and 1301; this TRamp does
	// on the bonus tables only, as Pinball.exe's doesn't.
	ControlRampExitToLayer2 = 68,
	ControlRampExitToLayer1 = 69,
	// Full Tilt only: a TAnimatedBlocker reached the state it was animating to (CADET.EXE's 0x3d)
	ControlAnimationDone = 70,
	// The bonus tables only: a TRamp's plane moved the ball to another layer (CADET.EXE's 0x54)
	ControlRampLayerChanged = 71,

	// Dragon's Keep's rules only, DRAGON.EXE's numbers, which its rules send only among themselves
	DragonSkipBonusText = 1,
	// The component that scored last no longer is, as another one scores (0x4d); Skulduggery's ramps and orbits end
	// their combos on it
	DragonScorerReplaced = 77,
	DragonEnterState = 79,
	DragonShowStateText = 80,
	DragonQuestResume = 81,
	DragonQuestEnd = 82,
	DragonBallDrained = 83,

	// Public codes 1000+, apply to all components
	LeftFlipperInputPressed = 1000,
	LeftFlipperInputReleased = 1001,
	RightFlipperInputPressed = 1002,
	RightFlipperInputReleased = 1003,
	PlungerInputPressed = 1004,
	PlungerInputReleased = 1005,
	Pause = 1008,
	Resume = 1009,
	LooseFocus = 1010,
	SetTiltLock = 1011,
	ClearTiltLock = 1012,
	StartGamePlayer1 = 1013,
	NewGame = 1014,
	PlungerFeedBall = 1015,
	PlungerStartFeedTimer = 1016,
	PlungerLaunchBall = 1017,
	PlungerRelaunchBall = 1018,
	PlayerChanged = 1020,
	SwitchToNextPlayer = 1021,
	GameOver = 1022,
	Reset = 1024,
};

class TPinballComponent
{
public:
	TPinballComponent(TPinballTable* table, int groupIndex, bool loadVisuals);
	virtual ~TPinballComponent();
	virtual int Message(MessageCode code, float value);
	virtual void port_draw();
	int get_scoring(unsigned int index) const;
	virtual vector2 get_coordinates();
	void SpriteSet(int index) const;
	void SpriteSetBall(int index, vector2i pos, float depth) const;
	// CADET.EXE, the bonus tables' engine, keeps every component's message field per player: a player change swaps it
	// (0x413570) and Reset clears it for all players (0x413550). Its components but the flippers do this in Message;
	// TLight, TLightGroup, TBumper, TPopupTarget and TSink already keep theirs per player, as 3D Pinball's do.
	void KeepMessageFieldPerPlayer(MessageCode code, float value);

	char UnusedBaseFlag;
	char ActiveFlag;
	int MessageField;
	std::array<int, 4> PlayerMessageFields{};
	char* GroupName;
	component_control* Control;
	int GroupIndex;
	render_sprite* RenderSprite;
	TPinballTable* PinballTable;
	std::vector<SpriteData>* ListBitmap;
private:
	float VisualPosNormX;
	float VisualPosNormY;
};
