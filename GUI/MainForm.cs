using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

namespace SuperSkillTool;

public class MainForm : Form
{
	private class TextBoxWriter : TextWriter
	{
		private TextBox _tb;

		public override Encoding Encoding => Encoding.UTF8;

		public TextBoxWriter(TextBox tb)
		{
			_tb = tb;
		}

		public override void Write(char value)
		{
			Append(value.ToString());
		}

		public override void Write(string value)
		{
			if (value != null)
			{
				Append(value);
			}
		}

		public override void WriteLine(string value)
		{
			Append(value + Environment.NewLine);
		}

		private void Append(string text)
		{
			if (_tb.IsDisposed)
			{
				return;
			}
			if (_tb.InvokeRequired)
			{
				try
				{
					if (!_tb.IsHandleCreated)
					{
						return;
					}
					_tb.BeginInvoke((Action)delegate
					{
						DoAppend(text);
					});
					return;
				}
				catch
				{
					return;
				}
			}
			DoAppend(text);
		}

		private void DoAppend(string text)
		{
			if (!_tb.IsDisposed)
			{
				_tb.AppendText(text);
			}
		}
	}

	private class TeeWriter : TextWriter
	{
		private TextWriter _a;

		private TextWriter _b;

		public override Encoding Encoding => _a.Encoding;

		public TeeWriter(TextWriter a, TextWriter b)
		{
			_a = a;
			_b = b;
		}

		public override void Write(char value)
		{
			_a.Write(value);
			_b.Write(value);
		}

		public override void Write(string value)
		{
			_a.Write(value);
			_b.Write(value);
		}

		public override void WriteLine(string value)
		{
			_a.WriteLine(value);
			_b.WriteLine(value);
		}

		public override void Flush()
		{
			_a.Flush();
			_b.Flush();
		}
	}

	private sealed class SkillQueueSnapshot
	{
		public List<SkillDefinition> PendingSkills = new List<SkillDefinition>();

		public List<SkillDefinition> DeletedSkills = new List<SkillDefinition>();

		public int? EditingListIndex;

		public EditStateSnapshot EditState;

		public FormStateSnapshot FormState;
	}

	private sealed class FormStateSnapshot
	{
		public string SkillId = "";

		public string Name = "";

		public string Desc = "";

		public int TabIndex;

		public decimal MaxLevel;

		public decimal SuperSpCost;

		public int PacketRouteIndex;

		public int ReleaseClassIndex;

		public string ProxySkillId = "";

		public string VisualSkillId = "";

		public string MountItemId = "";

		public int MountResourceModeIndex;

		public string MountSourceItemId = "";

		public string MountTamingMobId = "";

		public string MountSpeed = "";

		public string MountJump = "";

		public string MountFatigue = "";

		public bool BorrowDonorVisual;

		public bool HideFromNative;

		public bool ShowInNativeWhenLearned;

		public bool ShowInSuperWhenLearned;

		public bool AllowNativeFallback;

		public bool InjectToNative;

		public bool AllowMountedFlight;

		public int SelectedSkillIndex = -1;

		public int SelectedEffectIndex = -1;

		public string SelectedEffectNodeName = "effect";
	}

	private sealed class EditStateSnapshot
	{
		public WzSkillDataSnapshot LoadedData;

		public string IconOverrideBase64 = "";

		public string IconMOOverrideBase64 = "";

		public string IconDisOverrideBase64 = "";

		public List<EffectFrameSnapshot> EditedEffects = new List<EffectFrameSnapshot>();

		public Dictionary<string, List<EffectFrameSnapshot>> EditedEffectsByNode = new Dictionary<string, List<EffectFrameSnapshot>>(StringComparer.OrdinalIgnoreCase);

		public string SelectedEffectNodeName = "effect";

		public WzNodeInfo EditedTree;

		public Dictionary<int, Dictionary<string, string>> EditedLevelParams = new Dictionary<int, Dictionary<string, string>>();

		public bool HasManualEffectEdit;
	}

	private sealed class WzSkillDataSnapshot
	{
		public int SkillId;

		public int JobId;

		public string Name = "";

		public string Desc = "";

		public string PDesc = "";

		public string Ph = "";

		public string Action = "";

		public int InfoType;

		public Dictionary<string, string> HLevels = new Dictionary<string, string>();

		public Dictionary<int, Dictionary<string, string>> LevelParams = new Dictionary<int, Dictionary<string, string>>();

		public Dictionary<string, string> CommonParams = new Dictionary<string, string>();

		public string IconBase64 = "";

		public string IconMouseOverBase64 = "";

		public string IconDisabledBase64 = "";

		public WzNodeInfo RootNode;
	}

	private sealed class EffectFrameSnapshot
	{
		public int Index;

		public int Width;

		public int Height;

		public int Delay;

		public string BitmapBase64 = "";

		public Dictionary<string, WzFrameVector> Vectors = new Dictionary<string, WzFrameVector>(StringComparer.OrdinalIgnoreCase);

		public Dictionary<string, string> FrameProps = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
	}

	private sealed class SkillLibraryItem
	{
		public int SkillId;

		public int JobId = -1;

		public string Name = "";

		public string Desc = "";

		public string SearchText = "";

		public string Display => (string.IsNullOrWhiteSpace(Name) ? "(无名)" : Name) + "-" + SkillId;
	}

	private sealed class MountActionNodeItem
	{
		public string Key = "";

		public string Display = "";

		public override string ToString()
		{
			return Display;
		}
	}

	private static readonly string[] RouteNames = new string[8] { "close_range", "ranged_attack", "magic_attack", "special_move", "skill_effect", "cancel_buff", "special_attack", "passive_energy" };

	private static readonly string[] RouteLabels = new string[8]
	{
		"close_range (近战, 0x46)",
		"ranged_attack (远程, 0x47)",
		"magic_attack (魔法, 0x48)",
		"special_move (主动BUFF/飞行/骑宠/变身, 0x93)",
		"skill_effect (技能表现/特效, 0x95)",
		"cancel_buff (取消BUFF, 0x94)",
		"special_attack (特殊攻击, 0xAA)",
		"passive_energy (被动能量)"
	};

	private static readonly string[] ReleaseClassNames = new string[2] { "native_classifier_proxy", "native_b31722" };

	private static readonly string[] MountResourceModeNames = new string[3] { "config_only", "sync_action", "sync_action_and_data" };

	private static readonly string[] MountResourceModeLabels = new string[3]
	{
		"仅写配置（不改坐骑资源）",
		"缺失时同步动作（0190xxxx.img）",
		"同步动作+参数（0190xxxx.img + 000x.img）"
	};

	private static readonly string[] PresetNames = new string[5] { "1301008 - 冰骑近战", "3001005 - 远程模板", "2001004 - 魔法模板", "1001003 - 增益模板", "1001000 - 新手固定近战" };

	private static readonly int[] PresetIds = new int[5] { 1301008, 3001005, 2001004, 1001003, 1001000 };

	private static readonly Dictionary<string, string> MountActionNodeLabels = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
	{
		{ "walk1", "行走1" },
		{ "walk2", "行走2" },
		{ "stand1", "站立1" },
		{ "stand2", "站立2" },
		{ "jump", "跳跃" },
		{ "fly", "飞行" },
		{ "rope", "绳索" },
		{ "ladder", "梯子" },
		{ "tired", "疲劳" },
		{ "prone", "趴下" },
		{ "sit", "坐姿" },
		{ "alert", "警戒" },
		{ "default", "默认" },
		{ "ActionEffect", "动作特效" },
		{ "boost1", "加速1" },
		{ "boost2", "加速2" }
	};

	private static readonly string[] MountActionDefaultNodeKeys = new string[]
	{
		"default", "stand1", "stand2", "walk1", "walk2", "jump", "fly", "rope", "ladder", "sit", "prone", "tired", "alert", "boost1", "boost2", "ActionEffect"
	};

	private static readonly string[] MountActionInfoPresetKeys = new string[]
	{
		"tamingMob","icon","iconRaw","islot","vslot","reqJob","reqLevel","tuc","tradeBlock","notSale","only","cash","slotMax","timeLimited","dropBlock","type",
		"avatarCount","partsCount","partsQuestID","passengerNum","passengerNavel0","passengerNavel1","passengerNavel2",
		"vehicleSkillIsTown","vehicleNaviFlyingLevel","vehicleNewFlyingLevel","vehicleGlideLevel","vehicleDoubleJumpLevel",
		"doSkillActiveByAttackKey","forceCharacterAction","forceCharacterActionFrameIndex","forceCharacterEmotion","forceCharacterFace","forceCharacterFaceFrameIndex","forceCharacterFlip",
		"removeBody","removeJobWing","removeEffect","removeEffectAll","invisibleCape","invisibleWeapon",
		"hpRecovery","mpRecovery","incSpeed","incJump","incSwim","incFatigue","incSTR","incDEX","incINT","incLUK","incPAD","incPDD","incMDD","incMHP","incMMP","incEVA"
	};

	private static readonly string[] MountDataInfoPresetKeys = new string[]
	{
		"speed","jump","fs","swim","fatigue","continentMove","userSpeed","userJump"
	};

	private static readonly string[] BuiltinJobTextLines = new string[]
	{
		"新手(0)",
		"战士(100)", "剑客(110)", "勇士(111)", "英雄(112)",
		"准骑士(120)", "骑士(121)", "圣骑士(122)",
		"枪战士(130)", "龙骑士(131)", "黑骑士(132)",
		"魔法师(200)", "火毒巫师(210)", "火毒魔导师(211)", "火毒大魔导师(212)",
		"冰雷巫师(220)", "冰雷魔导师(221)", "冰雷大魔导师(222)",
		"牧师(230)", "祭司(231)", "主教(232)",
		"弓箭手(300)", "猎人(310)", "射手(311)", "神射手(312)",
		"弩弓手(320)", "游侠(321)", "箭神(322)",
		"飞侠(400)", "刺客(410)", "无影人(411)", "隐士(412)",
		"侠客(420)", "独行客(421)", "侠盗(422)",
		"海盗(500)", "拳手(510)", "斗士(511)", "冲锋队长(512)",
		"火枪手(520)", "大副(521)", "船长(522)",
		"管理员(800)", "GM(900)",
		"骑士团新手(1000)", "魂骑士1转(1100)", "魂骑士2转(1110)", "魂骑士3转(1111)", "魂骑士4转(1112)",
		"炎术士1转(1200)", "炎术士2转(1210)", "炎术士3转(1211)", "炎术士4转(1212)",
		"风灵使者1转(1300)", "风灵使者2转(1310)", "风灵使者3转(1311)", "风灵使者4转(1312)",
		"夜行者1转(1400)", "夜行者2转(1410)", "夜行者3转(1411)", "夜行者4转(1412)",
		"奇袭者1转(1500)", "奇袭者2转(1510)", "奇袭者3转(1511)", "奇袭者4转(1512)",
		"战神(2000)", "龙神(2001)", "双弩精灵(2002)", "幻影(2003)", "隐月(2005)",
		"恶魔猎手(3001)", "恶魔复仇者(3101)", "爆莉萌天使(6001)",
		"神之子(10000)", "虎影(16000)"
	};

	private static readonly List<KeyValuePair<string, int>> JobList = LoadJobList();

	private List<SkillDefinition> _pendingSkills = new List<SkillDefinition>();

	private List<SkillDefinition> _deletedSkills = new List<SkillDefinition>();

	private Stack<SkillQueueSnapshot> _undoStack = new Stack<SkillQueueSnapshot>();

	private Stack<SkillQueueSnapshot> _redoStack = new Stack<SkillQueueSnapshot>();

	private const int MaxUndoRedoDepth = 50;

	private WzImgLoader _wzLoader;

	private EditState _editState = new EditState();

	private TabControl tabMain;

	private ComboBox cboJobId;

	private TextBox txtSkillNum;

	private TextBox txtSkillIdInput;

	private TextBox txtSkillLibrarySearch;

	private CheckBox chkSkillFilterName;

	private CheckBox chkSkillFilterId;

	private CheckBox chkSkillFilterDesc;

	private ListView lvSkillLibrary;

	private Label lblSkillLibraryStatus;

	private Button btnLoad;

	private ComboBox cboPreset;

	private Button btnLoadPreset;

	private Label lblLoadStatus;

	private Label lblAction;

	private PictureBox picIcon;

	private PictureBox picIconMO;

	private PictureBox picIconDis;

	private TreeView treeSkillData;

	private DataGridView dgvLevelParams;

	private ListView lvEffectFrames;

	private ComboBox cboEffectNode;

	private TextBox txtSkillId;

	private TextBox txtName;

	private TextBox txtDesc;

	private ComboBox cboTab;

	private NumericUpDown nudMaxLevel;

	private NumericUpDown nudSuperSpCost;

	private ComboBox cboPacketRoute;

	private ComboBox cboReleaseClass;

	private TextBox txtProxySkillId;

	private TextBox txtVisualSkillId;

	private TextBox txtMountItemId;

	private ComboBox cboMountResourceMode;

	private TextBox txtMountSourceItemId;

	private TextBox txtMountTamingMobId;

	private TextBox txtMountSpeed;

	private TextBox txtMountJump;

	private TextBox txtMountFatigue;

	private CheckBox chkBorrowDonorVisual;

	private CheckBox chkAllowMountedFlight;

	private GroupBox grpRoute;

	private CheckBox chkHideFromNative;

	private CheckBox chkShowInNativeWhenLearned;

	private CheckBox chkShowInSuperWhenLearned;

	private CheckBox chkAllowNativeFallback;

	private CheckBox chkInjectToNative;

	private ListView lvSkills;

	private Button btnAddToList;

	private Button btnUpdateSelected;

	private Button btnRemoveFromList;

	private Button btnClearList;

	private Button btnUndo;

	private Button btnRedo;

	private Button btnExecuteAdd;

	private CheckBox chkSkipImg;

	private Button btnCopyEffects;

	private PictureBox _picEffectPreview;

	private TextBox txtMountEditorItemId;

	private TextBox txtMountEditorSourceItemId;

	private TextBox txtMountEditorDataId;

	private ComboBox cboMountActionNode;

	private ListView lvMountFrames;

	private PictureBox picMountFramePreview;

	private DataGridView dgvMountActionInfo;

	private DataGridView dgvMountDataInfo;

	private Button btnMountLoad;

	private Button btnMountClone;

	private Button btnMountSaveAction;

	private Button btnMountSaveData;

	private Button btnMountSaveAll;

	private Button btnMountSyncXml;

	private Button btnMountApplyToSkill;

	private Button btnMountAddNode;

	private Button btnMountRemoveNode;

	private Label lblMountEditorDataBinding;

	private ComboBox cboMountKnownIds;

	private Button btnMountKnownLoad;

	private Button btnMountKnownRefresh;

	private bool _suppressMountNodeChange;

	private MountEditorData _mountEditorData;

	private Label _lblParamType;

	private WzNodeInfo _clipboardNode;

	private List<WzEffectFrame> _clipboardFrames;

	private bool _suppressIdSync;

	private TextBox txtVerifyId;

	private TextBox txtRemoveId;

	private CheckBox chkRemoveDryRun;

	private TextBox txtServerRootDir;

	private TextBox txtOutputDir;

	private TextBox txtGameDataBaseDir;

	private TextBox txtConfigDataDir;

	private TextBox txtDefaultCarrierId;

	private TextBox txtLog;

	private bool _syncCarrierSkillText;

	private bool _isRestoringSnapshot;

	private bool _dgvCellEditUndoCaptured;

	private bool _suppressEffectNodeChange;

	private bool _hasManualEffectEdit;

	private ComboBox cboAnimLevel;

	private Button btnAddAnimLevel;

	private Button btnRemoveAnimLevel;

	private Button btnAddAnimNode;

	private Button btnDeleteAnimNode;

	private TextBox txtHTemplate;

	private TextBox txtPDesc;

	private TextBox txtPh;

	private DataGridView dgvHLevels;

	private TabControl tabTextEdit;
	private Label lblTextMode;

	private bool _suppressAnimLevelChange;

	private int _executeBusy;

	private ToolTip _toolTip;

	private int _lastParamTipRow = -2;

	private int _lastParamTipCol = -2;

	private string _lastParamTipText = "";

	private DataGridView _lastMountTipGrid;

	private int _lastMountTipRow = -2;

	private int _lastMountTipCol = -2;

	private string _lastMountTipText = "";

	private static string PendingSkillsJson => Path.Combine(PathConfig.ConfigDataDir, "pending_skills.json");

	private static string CustomMountIdsJson => Path.Combine(PathConfig.ConfigDataDir, "custom_mount_ids.json");

	private Dictionary<int, Dictionary<string, object>> _superSkillsCfgById = new Dictionary<int, Dictionary<string, object>>();

	private Dictionary<int, Dictionary<string, object>> _routesCfgById = new Dictionary<int, Dictionary<string, object>>();

	private readonly HashSet<int> _customMountKnownIds = new HashSet<int>();

	private Dictionary<int, Dictionary<string, object>> _injectionsCfgById = new Dictionary<int, Dictionary<string, object>>();

	private Dictionary<int, Dictionary<string, object>> _serverCfgById = new Dictionary<int, Dictionary<string, object>>();

	private List<SkillLibraryItem> _skillLibraryItems = new List<SkillLibraryItem>();

	private bool _skillLibraryLoading;

	private static List<KeyValuePair<string, int>> LoadJobList()
	{
		List<KeyValuePair<string, int>> list = new List<KeyValuePair<string, int>>();
		HashSet<int> seen = new HashSet<int>();

		void AddLine(string line)
		{
			if (string.IsNullOrWhiteSpace(line))
			{
				return;
			}
			string input = line.Trim().TrimEnd(',').TrimEnd(';');
			Match match = Regex.Match(input, "^(.+?)\\((\\d+)\\)");
			if (!match.Success)
			{
				return;
			}
			int value = int.Parse(match.Groups[2].Value);
			if (seen.Add(value))
			{
				list.Add(new KeyValuePair<string, int>(match.Groups[1].Value + "(" + value + ")", value));
			}
		}

		// 1) Built-in job text (hardcoded, no external dependency)
		foreach (string line in (BuiltinJobTextLines ?? Array.Empty<string>()))
		{
			AddLine(line);
		}

		// 2) Optional external file merge (for custom naming)
		string[] array = new string[5]
		{
			Path.Combine(PathConfig.ToolRoot, "职业ID.txt"),
			Path.Combine(PathConfig.ToolRoot, "docs", "职业ID.txt"),
			Path.Combine(AppContext.BaseDirectory, "职业ID.txt"),
			Path.Combine(AppContext.BaseDirectory, "docs", "职业ID.txt"),
			Path.Combine(Environment.CurrentDirectory, "职业ID.txt")
		};
		string text = null;
		foreach (string text2 in array)
		{
			if (File.Exists(text2))
			{
				text = text2;
				break;
			}
		}
		if (string.IsNullOrEmpty(text))
		{
			return list;
		}
		foreach (string line in File.ReadAllLines(text, Encoding.UTF8))
		{
			AddLine(line);
		}
		if (list.Count == 0)
		{
			try
			{
				foreach (string line2 in File.ReadAllLines(text, Encoding.Default))
				{
					AddLine(line2);
				}
			}
			catch
			{
			}
		}
		return list;
	}

	private void RefreshJobIdComboItems()
	{
		List<KeyValuePair<string, int>> latest = LoadJobList();
		if (latest == null || latest.Count == 0)
		{
			return;
		}

		int? oldJobId = null;
		if (cboJobId != null)
		{
			int oldIndex = cboJobId.SelectedIndex;
			if (oldIndex >= 0 && oldIndex < JobList.Count)
			{
				oldJobId = JobList[oldIndex].Value;
			}
		}

		JobList.Clear();
		JobList.AddRange(latest);

		if (cboJobId == null)
		{
			return;
		}

		cboJobId.BeginUpdate();
		try
		{
			cboJobId.Items.Clear();
			foreach (KeyValuePair<string, int> job in JobList)
			{
				cboJobId.Items.Add(job.Key);
			}

			int targetIndex = -1;
			if (oldJobId.HasValue)
			{
				for (int i = 0; i < JobList.Count; i++)
				{
					if (JobList[i].Value == oldJobId.Value)
					{
						targetIndex = i;
						break;
					}
				}
			}
			if (targetIndex < 0 && JobList.Count > 0)
			{
				targetIndex = 0;
			}
			if (targetIndex >= 0)
			{
				cboJobId.SelectedIndex = targetIndex;
			}
		}
		finally
		{
			cboJobId.EndUpdate();
		}
	}

	public MainForm()
	{
		_wzLoader = new WzImgLoader();
		InitializeComponent();
		Console.SetOut(new TextBoxWriter(txtLog));
		Console.SetError(new TextBoxWriter(txtLog));
		Console.WriteLine("[GUI] Build marker: 2026-04-08-param-tip-force-visible");
		LoadConfigSnapshots();
		RefreshJobIdComboItems();
		LoadPendingFromJson();
		LoadSuperSkillsFromImg();
		LoadSkillLibrary(forceReload: true);
		LoadCustomMountIds();
		RefreshMountKnownIds();
		SyncCarrierSkillIdEditors();
		if (RemoveCarrierSkillsFromQueues() > 0)
		{
			RefreshListView();
			SavePendingList();
		}
	}

	protected override void Dispose(bool disposing)
	{
		if (disposing)
		{
			_toolTip?.Dispose();
			_mountEditorData?.Dispose();
			_editState?.Clear();
			_wzLoader?.Dispose();
		}
		base.Dispose(disposing);
	}

	private void InitializeComponent()
	{
		this.Text = "超级技能工具";
		base.Size = new System.Drawing.Size(1100, 1000);
		this.MinimumSize = new System.Drawing.Size(900, 800);
		base.StartPosition = System.Windows.Forms.FormStartPosition.CenterScreen;
		base.KeyPreview = true;
		base.KeyDown += MainForm_KeyDown;
		try
		{
			this.Font = new System.Drawing.Font("Microsoft YaHei UI", 9f);
		}
		catch
		{
		}
		const int defaultLogPanelHeight = 80;
		System.Windows.Forms.SplitContainer splitContainer = new System.Windows.Forms.SplitContainer
		{
			Dock = System.Windows.Forms.DockStyle.Fill,
			Orientation = System.Windows.Forms.Orientation.Horizontal,
			FixedPanel = System.Windows.Forms.FixedPanel.Panel2,
			Panel2MinSize = 60
		};
		splitContainer.HandleCreated += delegate
		{
			int total = splitContainer.ClientSize.Height;
			if (total <= 0)
			{
				return;
			}
			int desiredBottom = Math.Max(splitContainer.Panel2MinSize, defaultLogPanelHeight);
			int desiredTop = Math.Max(0, total - desiredBottom);
			if (desiredTop < splitContainer.Panel1MinSize)
			{
				desiredTop = splitContainer.Panel1MinSize;
			}
			splitContainer.SplitterDistance = desiredTop;
		};
		base.Controls.Add(splitContainer);
		this.tabMain = new System.Windows.Forms.TabControl
		{
			Dock = System.Windows.Forms.DockStyle.Fill
		};
		splitContainer.Panel1.Controls.Add(this.tabMain);
		this.txtLog = new System.Windows.Forms.TextBox
		{
			Dock = System.Windows.Forms.DockStyle.Fill,
			Multiline = true,
			ReadOnly = true,
			ScrollBars = System.Windows.Forms.ScrollBars.Both,
			BackColor = System.Drawing.Color.FromArgb(30, 30, 30),
			ForeColor = System.Drawing.Color.FromArgb(200, 220, 200),
			WordWrap = false
		};
		try
		{
			this.txtLog.Font = new System.Drawing.Font("Consolas", 9f);
		}
		catch
		{
		}
		_toolTip = new ToolTip
		{
			AutoPopDelay = 12000,
			InitialDelay = 300,
			ReshowDelay = 120,
			ShowAlways = true,
			IsBalloon = false,
			UseAnimation = true,
			UseFading = true
		};
		splitContainer.Panel2.Controls.Add(this.txtLog);
		this.BuildTab1_SkillEditor();
		this.BuildTab2_Management();
		this.BuildTab3_Settings();
		this.BuildTab4_MountEditor();
		ConfigureToolTips();
		RefreshAnimLevelSelector(preserveSelection: false);
		RefreshEffectNodeSelector("effect", createIfMissing: true);
		UpdateUndoRedoButtons();
	}

	private void BuildTab1_SkillEditor()
	{
		TabPage tabPage = new TabPage("技能编辑");
		tabMain.TabPages.Add(tabPage);
		SplitContainer splitContainer = new SplitContainer
		{
			Dock = DockStyle.Fill,
			Orientation = Orientation.Horizontal
		};
		tabPage.Controls.Add(splitContainer);
		SplitContainer splitContainer2 = new SplitContainer
		{
			Dock = DockStyle.Fill,
			Orientation = Orientation.Vertical,
			FixedPanel = FixedPanel.Panel1
		};
		splitContainer.Panel1.Controls.Add(splitContainer2);
		BuildSkillLibraryPanel(splitContainer2.Panel1);
		void ApplySkillLibrarySplitLayout()
		{
			int totalWidth = splitContainer2.ClientSize.Width;
			if (totalWidth <= 0)
			{
				return;
			}
			int minLeft = 96;
			int minRight = 700;
			if (minLeft + minRight > totalWidth)
			{
				minLeft = Math.Max(70, totalWidth / 6);
				minRight = Math.Max(120, totalWidth - minLeft);
			}
			splitContainer2.Panel1MinSize = Math.Max(0, minLeft);
			splitContainer2.Panel2MinSize = Math.Max(0, minRight);
			int maxLeft = Math.Max(splitContainer2.Panel1MinSize, totalWidth - splitContainer2.Panel2MinSize);
			int desiredLeft = Math.Min(132, maxLeft);
			if (desiredLeft < splitContainer2.Panel1MinSize)
			{
				desiredLeft = splitContainer2.Panel1MinSize;
			}
			int limit = Math.Max(splitContainer2.Panel1MinSize, totalWidth - splitContainer2.Panel2MinSize);
			if (desiredLeft > limit)
			{
				desiredLeft = limit;
			}
			splitContainer2.SplitterDistance = desiredLeft;
		}
		void ApplyEditorSplitLayout()
		{
			int total = splitContainer.ClientSize.Height;
			if (total <= 0)
			{
				return;
			}
			int minTop = 320;
			int minBottom = 150;
			if (minTop + minBottom > total)
			{
				minTop = Math.Max(80, total * 2 / 3);
				minBottom = Math.Max(60, total - minTop);
			}
			splitContainer.Panel1MinSize = Math.Max(0, minTop);
			splitContainer.Panel2MinSize = Math.Max(0, minBottom);
			int maxTop = Math.Max(splitContainer.Panel1MinSize, total - splitContainer.Panel2MinSize);
			int desiredTop = Math.Min(650, maxTop);
			if (desiredTop < splitContainer.Panel1MinSize)
			{
				desiredTop = splitContainer.Panel1MinSize;
			}
			int limit = Math.Max(splitContainer.Panel1MinSize, total - splitContainer.Panel2MinSize);
			if (desiredTop > limit)
			{
				desiredTop = limit;
			}
			splitContainer.SplitterDistance = desiredTop;
		}
		splitContainer.Resize += delegate
		{
			ApplyEditorSplitLayout();
		};
		splitContainer2.Resize += delegate
		{
			ApplySkillLibrarySplitLayout();
		};
		tabPage.Enter += delegate
		{
			ApplySkillLibrarySplitLayout();
			ApplyEditorSplitLayout();
		};
		splitContainer2.HandleCreated += delegate
		{
			ApplySkillLibrarySplitLayout();
		};
		splitContainer.HandleCreated += delegate
		{
			ApplyEditorSplitLayout();
		};
		Panel panel = new Panel
		{
			Dock = DockStyle.Fill,
			AutoScroll = true
		};
		splitContainer2.Panel2.Controls.Add(panel);
		Panel panel2 = new Panel
		{
			Dock = DockStyle.Fill,
			Padding = new Padding(8, 6, 8, 8)
		};
		splitContainer.Panel2.Controls.Add(panel2);
		int num = 8;
		int num2 = 540;
		int num3 = num2 + 10;
		int num12 = num2 - 10;
		int rightEditorWidth = Math.Max(220, num12 * 3 / 4);
		int treeEditorHeight = 240;
		int paramEditorHeight = 255;
		int requiredEditorPanelWidth = num3 + rightEditorWidth + 12;
		int requiredClientWidth = 132 + requiredEditorPanelWidth + 24;
		if (base.ClientSize.Width < requiredClientWidth)
		{
			base.ClientSize = new Size(requiredClientWidth, base.ClientSize.Height);
		}
		if (this.MinimumSize.Width < requiredClientWidth)
		{
			this.MinimumSize = new Size(requiredClientWidth, this.MinimumSize.Height);
		}
		panel.Controls.Add(new Label
		{
			Text = "职业:",
			Location = new Point(10, num + 3),
			AutoSize = true
		});
		cboJobId = new ComboBox
		{
			Location = new Point(46, num),
			Width = 160,
			DropDownStyle = ComboBoxStyle.DropDownList
		};
		foreach (KeyValuePair<string, int> job in JobList)
		{
			cboJobId.Items.Add(job.Key);
		}
		if (cboJobId.Items.Count > 0)
		{
			cboJobId.SelectedIndex = 0;
		}
		cboJobId.SelectedIndexChanged += delegate
		{
			SyncSkillIdFromJobAndNum();
		};
		panel.Controls.Add(cboJobId);
		panel.Controls.Add(new Label
		{
			Text = "编号:",
			Location = new Point(212, num + 3),
			AutoSize = true
		});
		txtSkillNum = new TextBox
		{
			Location = new Point(248, num),
			Width = 60
		};
		txtSkillNum.TextChanged += delegate
		{
			SyncSkillIdFromJobAndNum();
		};
		panel.Controls.Add(txtSkillNum);
		panel.Controls.Add(new Label
		{
			Text = "技能ID:",
			Location = new Point(316, num + 3),
			AutoSize = true
		});
		txtSkillIdInput = new TextBox
		{
			Location = new Point(370, num),
			Width = 90
		};
		txtSkillIdInput.TextChanged += delegate
		{
			SyncJobAndNumFromSkillId();
		};
		txtSkillIdInput.KeyDown += delegate(object s, KeyEventArgs e)
		{
			if (e.KeyCode == Keys.Return)
			{
				DoLoadSkill();
				e.SuppressKeyPress = true;
			}
		};
		panel.Controls.Add(txtSkillIdInput);
		btnLoad = new Button
		{
			Text = "加载",
			Location = new Point(465, num - 1),
			Width = 55
		};
		btnLoad.Click += delegate
		{
			DoLoadSkill();
		};
		panel.Controls.Add(btnLoad);
		lblLoadStatus = new Label
		{
			Text = "",
			Location = new Point(525, num + 3),
			AutoSize = true,
			ForeColor = Color.Gray
		};
		panel.Controls.Add(lblLoadStatus);
		num += 28;
		panel.Controls.Add(new Label
		{
			Text = "预设:",
			Location = new Point(10, num + 3),
			AutoSize = true
		});
		cboPreset = new ComboBox
		{
			Location = new Point(46, num),
			Width = 180,
			DropDownStyle = ComboBoxStyle.DropDownList
		};
		for (int num4 = 0; num4 < PresetNames.Length; num4++)
		{
			cboPreset.Items.Add(PresetNames[num4]);
		}
		if (cboPreset.Items.Count > 0)
		{
			cboPreset.SelectedIndex = 0;
		}
		panel.Controls.Add(cboPreset);
		btnLoadPreset = new Button
		{
			Text = "加载预设",
			Location = new Point(232, num - 1),
			Width = 75
		};
		btnLoadPreset.Click += delegate
		{
			int selectedIndex = cboPreset.SelectedIndex;
			if (selectedIndex >= 0 && selectedIndex < PresetIds.Length)
			{
				txtSkillIdInput.Text = PresetIds[selectedIndex].ToString();
				DoLoadSkill();
			}
		};
		panel.Controls.Add(btnLoadPreset);
		num += 30;
		int num5 = num;
		panel.Controls.Add(new Label
		{
			Text = "图标",
			Location = new Point(20, num5),
			AutoSize = true,
			ForeColor = Color.Gray
		});
		picIcon = MakeEditablePicBox(10, num5 + 16+5);
		panel.Controls.Add(picIcon);
		panel.Controls.Add(new Label
		{
			Text = "鼠标悬停",
			Location = new Point(84, num5),
			AutoSize = true,
			ForeColor = Color.Gray
		});
		picIconMO = MakeEditablePicBox(84, num5 + 16+5);
		panel.Controls.Add(picIconMO);
		panel.Controls.Add(new Label
		{
			Text = "禁用",
			Location = new Point(168, num5),
			AutoSize = true,
			ForeColor = Color.Gray
		});
		picIconDis = MakeEditablePicBox(158, num5 + 16+5);
		panel.Controls.Add(picIconDis);
		lblAction = new Label
		{
			Text = "",
			Location = new Point(240, num5 + 40),
			AutoSize = true,
			ForeColor = Color.CadetBlue
		};
		panel.Controls.Add(lblAction);
		num = num5 + 86;
		GroupBox groupBox = new GroupBox
		{
			Text = "基本信息",
			Location = new Point(8, num),
			Size = new Size(num12, 95)
		};
		int num6 = 18;
		groupBox.Controls.Add(new Label
		{
			Text = "技能ID:",
			Location = new Point(8, num6 + 3),
			AutoSize = true
		});
		txtSkillId = new TextBox
		{
			Location = new Point(62, num6),
			Width = 90
		};
		groupBox.Controls.Add(txtSkillId);
		groupBox.Controls.Add(new Label
		{
			Text = "名称:",
			Location = new Point(160, num6 + 3),
			AutoSize = true
		});
		txtName = new TextBox
		{
			Location = new Point(195, num6),
			Width = 130
		};
		groupBox.Controls.Add(txtName);
		groupBox.Controls.Add(new Label
		{
			Text = "分类:",
			Location = new Point(335, num6 + 3),
			AutoSize = true
		});
		cboTab = new ComboBox
		{
			Location = new Point(375, num6),
			Width = 80,
			DropDownStyle = ComboBoxStyle.DropDownList
		};
		cboTab.Items.AddRange("active", "passive");
		cboTab.SelectedIndex = 0;
		groupBox.Controls.Add(cboTab);
		num6 += 28;
		groupBox.Controls.Add(new Label
		{
			Text = "最大等级:",
			Location = new Point(8, num6 + 3),
			AutoSize = true
		});
		nudMaxLevel = new NumericUpDown
		{
			Location = new Point(72, num6),
			Width = 55,
			Minimum = 1m,
			Maximum = 30m,
			Value = 20m
		};
		nudMaxLevel.ValueChanged += NudMaxLevel_ValueChanged;
		groupBox.Controls.Add(nudMaxLevel);
		groupBox.Controls.Add(new Label
		{
			Text = "SP消耗:",
			Location = new Point(135, num6 + 3),
			AutoSize = true
		});
		nudSuperSpCost = new NumericUpDown
		{
			Location = new Point(190, num6),
			Width = 50,
			Minimum = 1m,
			Maximum = 10m,
			Value = 1m
		};
		groupBox.Controls.Add(nudSuperSpCost);
		groupBox.Controls.Add(new Label
		{
			Text = "描述:",
			Location = new Point(250, num6 + 3),
			AutoSize = true
		});
		txtDesc = new TextBox
		{
			Location = new Point(285, num6),
			Width = 210
		};
		groupBox.Controls.Add(txtDesc);
		panel.Controls.Add(groupBox);
		num += 100;
		grpRoute = new GroupBox
		{
			Text = "发包路由",
			Location = new Point(8, num),
			Size = new Size(num12, 176)
		};
		num6 = 18;
		grpRoute.Controls.Add(new Label
		{
			Text = "释放类型:",
			Location = new Point(8, num6 + 3),
			AutoSize = true
		});
		cboPacketRoute = new ComboBox
		{
			Location = new Point(72, num6),
			Width = 120,
			DropDownWidth = 340,
			DropDownStyle = ComboBoxStyle.DropDownList
		};
		for (int num7 = 0; num7 < RouteNames.Length; num7++)
		{
			cboPacketRoute.Items.Add(RouteLabels[num7]);
		}
		cboPacketRoute.SelectedIndex = 0;
		grpRoute.Controls.Add(cboPacketRoute);
		grpRoute.Controls.Add(new Label
		{
			Text = "代理技能ID:",
			Location = new Point(200, num6 + 3),
			AutoSize = true
		});
		txtProxySkillId = new TextBox
		{
			Location = new Point(270, num6),
			Width = 80
		};
		grpRoute.Controls.Add(txtProxySkillId);
		num6 += 26;
		grpRoute.Controls.Add(new Label
		{
			Text = "外观技能ID:",
			Location = new Point(8, num6 + 3),
			AutoSize = true
		});
		txtVisualSkillId = new TextBox
		{
			Location = new Point(80, num6),
			Width = 75
		};
		grpRoute.Controls.Add(txtVisualSkillId);
		Button button = new Button
		{
			Text = "加载外观",
			Location = new Point(160, num6 - 1),
			Width = 75
		};
		button.Click += BtnLoadVisual_Click;
		grpRoute.Controls.Add(button);
		grpRoute.Controls.Add(new Label
		{
			Text = "释放类别:",
			Location = new Point(245, num6 + 3),
			AutoSize = true
		});
		cboReleaseClass = new ComboBox
		{
			Location = new Point(310, num6),
			Width = 165,
			DropDownStyle = ComboBoxStyle.DropDownList
		};
		string[] releaseClassNames = ReleaseClassNames;
		foreach (string item in releaseClassNames)
		{
			cboReleaseClass.Items.Add(item);
		}
		cboReleaseClass.SelectedIndex = 0;
		grpRoute.Controls.Add(cboReleaseClass);
		num6 += 26;
		chkBorrowDonorVisual = new CheckBox
		{
			Text = "借用代理外观",
			Location = new Point(8, num6),
			AutoSize = true
		};
		grpRoute.Controls.Add(chkBorrowDonorVisual);
		num6 += 26;
		grpRoute.Controls.Add(new Label
		{
			Text = "坐骑ItemId:",
			Location = new Point(8, num6 + 3),
			AutoSize = true
		});
		txtMountItemId = new TextBox
		{
			Location = new Point(85, num6),
			Width = 75
		};
		grpRoute.Controls.Add(txtMountItemId);
		grpRoute.Controls.Add(new Label
		{
			Text = "资源模式:",
			Location = new Point(165, num6 + 3),
			AutoSize = true
		});
		cboMountResourceMode = new ComboBox
		{
			Location = new Point(225, num6),
			Width = 250,
			DropDownStyle = ComboBoxStyle.DropDownList
		};
		foreach (string mountResourceModeLabel in MountResourceModeLabels)
		{
			cboMountResourceMode.Items.Add(mountResourceModeLabel);
		}
		cboMountResourceMode.SelectedIndex = 0;
		grpRoute.Controls.Add(cboMountResourceMode);
		num6 += 26;
		chkAllowMountedFlight = new CheckBox
		{
			Text = "骑宠可飞行",
			Location = new Point(8, num6 + 1),
			AutoSize = true
		};
		grpRoute.Controls.Add(chkAllowMountedFlight);
		Button buttonMountEditor = new Button
		{
			Text = "打开坐骑编辑",
			Location = new Point(105, num6 - 1),
			Width = 120
		};
		buttonMountEditor.Click += delegate
		{
			if (!int.TryParse((txtMountItemId?.Text ?? "").Trim(), out int mountItemId) || mountItemId <= 0)
			{
				MessageBox.Show("请先填写坐骑ItemId", "提示");
				return;
			}

			if (txtMountEditorItemId != null)
				txtMountEditorItemId.Text = mountItemId.ToString();
			if (tabMain != null && tabMain.TabPages.Count > 3)
				tabMain.SelectedIndex = 3;
			BtnMountLoad_Click(this, EventArgs.Empty);
		};
		grpRoute.Controls.Add(buttonMountEditor);

		grpRoute.Controls.Add(new Label
		{
			Text = "高级参数自动从坐骑资源读取（tamingMob/Data/speed/jump/fatigue）",
			Location = new Point(232, num6 + 3),
			AutoSize = true,
			ForeColor = Color.Gray
		});

		// Keep hidden fields for compatibility with existing state/serialization flow.
		txtMountSourceItemId = new TextBox { Visible = false };
		txtMountTamingMobId = new TextBox { Visible = false };
		txtMountSpeed = new TextBox { Visible = false };
		txtMountJump = new TextBox { Visible = false };
		txtMountFatigue = new TextBox { Visible = false };
		panel.Controls.Add(grpRoute);
		num += 180;
		GroupBox groupBox2 = new GroupBox
		{
			Text = "显示策略",
			Location = new Point(8, num),
			Size = new Size(num12, 50)
		};
		int num9 = 8;
		chkHideFromNative = new CheckBox
		{
			Text = "原生隐藏",
			Location = new Point(num9, 20),
			AutoSize = true,
			Checked = true
		};
		groupBox2.Controls.Add(chkHideFromNative);
		num9 += 90;
		chkShowInNativeWhenLearned = new CheckBox
		{
			Text = "学习后显示在原生",
			Location = new Point(num9, 20),
			AutoSize = true
		};
		groupBox2.Controls.Add(chkShowInNativeWhenLearned);
		num9 += 125;
		chkShowInSuperWhenLearned = new CheckBox
		{
			Text = "学习后显示在超技",
			Location = new Point(num9, 20),
			AutoSize = true
		};
		groupBox2.Controls.Add(chkShowInSuperWhenLearned);
		num9 += 125;
		chkAllowNativeFallback = new CheckBox
		{
			Text = "允许原生回退",
			Location = new Point(num9, 20),
			AutoSize = true,
			Checked = true
		};
		groupBox2.Controls.Add(chkAllowNativeFallback);
		num9 += 100;
		chkInjectToNative = new CheckBox
		{
			Text = "注入原生",
			Location = new Point(num9, 20),
			AutoSize = true,
			Checked = true
		};
		groupBox2.Controls.Add(chkInjectToNative);
		panel.Controls.Add(groupBox2);
		num += 56;
		int num10 = num + 8;
		panel.Controls.Add(new Label
		{
			Text = "动画编辑（拖入图片可添加帧）:",
			Location = new Point(8, num10),
			AutoSize = true
		});
		btnCopyEffects = new Button
		{
			Text = "从其他技能复制",
			Location = new Point(190, num10 - 3),
			Width = 110
		};
		btnCopyEffects.Click += BtnCopyEffects_Click;
		panel.Controls.Add(btnCopyEffects);
		// Second row: level selector + node selector
		int num10r2 = num10 + 22;
		panel.Controls.Add(new Label
		{
			Text = "等级:",
			Location = new Point(8, num10r2 + 3),
			AutoSize = true
		});
		cboAnimLevel = new ComboBox
		{
			Location = new Point(43, num10r2),
			Width = 90,
			DropDownStyle = ComboBoxStyle.DropDownList
		};
		cboAnimLevel.SelectedIndexChanged += CboAnimLevel_SelectedIndexChanged;
		panel.Controls.Add(cboAnimLevel);
		btnAddAnimLevel = new Button
		{
			Text = "+",
			Location = new Point(136, num10r2),
			Size = new Size(22, 22),
			ForeColor = Color.LimeGreen,
			FlatStyle = FlatStyle.Flat,
			Padding = Padding.Empty
		};
		btnAddAnimLevel.Click += BtnAddAnimLevel_Click;
		panel.Controls.Add(btnAddAnimLevel);
		btnRemoveAnimLevel = new Button
		{
			Text = "-",
			Location = new Point(159, num10r2),
			Size = new Size(22, 22),
			ForeColor = Color.OrangeRed,
			FlatStyle = FlatStyle.Flat,
			Padding = Padding.Empty
		};
		btnRemoveAnimLevel.Click += BtnRemoveAnimLevel_Click;
		panel.Controls.Add(btnRemoveAnimLevel);
		panel.Controls.Add(new Label
		{
			Text = "节点:",
			Location = new Point(188, num10r2 + 3),
			AutoSize = true
		});
		cboEffectNode = new ComboBox
		{
			Location = new Point(223, num10r2),
			Width = 90,
			DropDownStyle = ComboBoxStyle.DropDownList
		};
		cboEffectNode.SelectedIndexChanged += CboEffectNode_SelectedIndexChanged;
		panel.Controls.Add(cboEffectNode);
		// Add/Delete node buttons next to node selector
		btnAddAnimNode = new Button
		{
			Text = "+节点",
			Location = new Point(317, num10r2),
			Size = new Size(50, 22),
			ForeColor = Color.LimeGreen
		};
		btnAddAnimNode.Click += BtnAddAnimNode_Click;
		panel.Controls.Add(btnAddAnimNode);
		btnDeleteAnimNode = new Button
		{
			Text = "-节点",
			Location = new Point(369, num10r2),
			Size = new Size(50, 22),
			ForeColor = Color.OrangeRed
		};
		btnDeleteAnimNode.Click += BtnDeleteAnimNode_Click;
		panel.Controls.Add(btnDeleteAnimNode);
		int num10c = num10r2 + 26;
		lvEffectFrames = new ListView
		{
			Location = new Point(8, num10c),
			Size = new Size(num12 - 102, 150),
			View = View.Details,
			FullRowSelect = true,
			MultiSelect = true,
			GridLines = true,
			AllowDrop = true
		};
		lvEffectFrames.Columns.Add("帧", 50);
		lvEffectFrames.Columns.Add("尺寸", 80);
		lvEffectFrames.Columns.Add("延迟(ms)", 70);
		lvEffectFrames.Columns.Add("定位/参数", 260);
		lvEffectFrames.SelectedIndexChanged += LvEffectFrames_SelectedChanged;
		lvEffectFrames.DragEnter += FxList_DragEnter;
		lvEffectFrames.DragDrop += FxList_DragDrop;
		ContextMenuStrip contextMenuStrip3 = new ContextMenuStrip();
		contextMenuStrip3.Items.Add("添加帧", null, FxMenu_AddFrame);
		contextMenuStrip3.Items.Add("替换帧图片", null, FxMenu_ReplaceFrame);
		contextMenuStrip3.Items.Add("编辑延迟", null, FxMenu_EditDelay);
		contextMenuStrip3.Items.Add("编辑定位/参数(x,y,z...)", null, FxMenu_EditFrameMeta);
		contextMenuStrip3.Items.Add(new ToolStripSeparator());
		contextMenuStrip3.Items.Add("复制选中帧", null, FxMenu_CopyFrames);
		contextMenuStrip3.Items.Add("粘贴帧", null, FxMenu_PasteFrames);
		contextMenuStrip3.Items.Add(new ToolStripSeparator());
		contextMenuStrip3.Items.Add("删除帧", null, FxMenu_DeleteFrame);
		lvEffectFrames.ContextMenuStrip = contextMenuStrip3;
		panel.Controls.Add(lvEffectFrames);
		_picEffectPreview = new PictureBox
		{
			Location = new Point(8 + num12 - 94, num10c),
			Size = new Size(90, 90),
			SizeMode = PictureBoxSizeMode.Zoom,
			BorderStyle = BorderStyle.FixedSingle,
			BackColor = Color.FromArgb(40, 40, 40),
			AllowDrop = true
		};
		_picEffectPreview.DragEnter += FxList_DragEnter;
		_picEffectPreview.DragDrop += FxList_DragDrop;
		panel.Controls.Add(_picEffectPreview);
		panel.Controls.Add(new Label
		{
			Text = "预览",
			Location = new Point(8 + num12 - 68, num10c + 94),
			AutoSize = true,
			ForeColor = Color.Gray
		});
		// (Text editing TabControl is placed in right column — see below)
		Label labelNodeTree = new Label
		{
			Text = "节点树（双击编辑值；右键菜单）:",
			Location = new Point(num3, 43),
			AutoSize = true,
			Anchor = (AnchorStyles.Top | AnchorStyles.Right)
		};
		panel.Controls.Add(labelNodeTree);
		treeSkillData = new TreeView
		{
			Location = new Point(num3, 63),
			Size = new Size(rightEditorWidth, treeEditorHeight),
			Anchor = (AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Right)
		};
		treeSkillData.NodeMouseDoubleClick += TreeNode_DoubleClick;
		ContextMenuStrip contextMenuStrip = new ContextMenuStrip();
		contextMenuStrip.Items.Add("编辑值", null, TreeMenu_EditValue);
		contextMenuStrip.Items.Add("复制节点", null, TreeMenu_CopyNode);
		contextMenuStrip.Items.Add("粘贴节点", null, TreeMenu_PasteNode);
		contextMenuStrip.Items.Add("添加子节点", null, TreeMenu_AddChild);
		contextMenuStrip.Items.Add("删除节点", null, TreeMenu_DeleteNode);
		treeSkillData.ContextMenuStrip = contextMenuStrip;
		panel.Controls.Add(treeSkillData);
		_lblParamType = new Label
		{
			Text = "技能参数",
			Location = new Point(num3, treeSkillData.Bottom + 8),
			AutoSize = true,
			Anchor = (AnchorStyles.Top | AnchorStyles.Right)
		};
		panel.Controls.Add(_lblParamType);
		dgvLevelParams = new DataGridView
		{
			Location = new Point(num3, _lblParamType.Bottom + 4),
			Size = new Size(rightEditorWidth, paramEditorHeight),
			Anchor = (AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Right),
			ReadOnly = false,
			AllowUserToAddRows = false,
			AllowUserToDeleteRows = false,
			RowHeadersVisible = false,
			AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill,
			BackgroundColor = Color.FromArgb(45, 45, 45),
			ForeColor = Color.White,
			GridColor = Color.FromArgb(70, 70, 70)
		};
		dgvLevelParams.DefaultCellStyle.BackColor = Color.FromArgb(45, 45, 45);
		dgvLevelParams.ColumnHeadersDefaultCellStyle.BackColor = Color.FromArgb(60, 60, 60);
		dgvLevelParams.ColumnHeadersDefaultCellStyle.ForeColor = Color.White;
		dgvLevelParams.EnableHeadersVisualStyles = false;
		ContextMenuStrip contextMenuStrip2 = new ContextMenuStrip();
		contextMenuStrip2.Items.Add("添加参数列", null, DgvMenu_AddColumn);
		contextMenuStrip2.Items.Add("删除参数列", null, DgvMenu_DeleteColumn);
		contextMenuStrip2.Items.Add("添加等级行", null, DgvMenu_AddRow);
		contextMenuStrip2.Items.Add("删除等级行", null, DgvMenu_DeleteRow);
		dgvLevelParams.ContextMenuStrip = contextMenuStrip2;
		dgvLevelParams.CellBeginEdit += DgvLevelParams_CellBeginEdit;
		dgvLevelParams.CellEndEdit += DgvLevelParams_CellEndEdit;
		dgvLevelParams.CellMouseEnter += DgvLevelParams_CellMouseEnter;
		dgvLevelParams.MouseMove += DgvLevelParams_MouseMove;
		dgvLevelParams.MouseLeave += DgvLevelParams_MouseLeave;
		panel.Controls.Add(dgvLevelParams);

		// ── Text editing section (right column, below params) ──
		lblTextMode = new Label
		{
			Text = "技能文本:",
			Location = new Point(num3, dgvLevelParams.Bottom + 8),
			AutoSize = true,
			Anchor = AnchorStyles.Top | AnchorStyles.Right
		};
		panel.Controls.Add(lblTextMode);

		tabTextEdit = new TabControl
		{
			Location = new Point(num3, lblTextMode.Bottom + 4),
			Size = new Size(rightEditorWidth, 200),
			Font = new Font("Microsoft YaHei UI", 8.5f),
			Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Right
		};
		panel.Controls.Add(tabTextEdit);

		// ── Tab 1: 通用文本 (h / pdesc / ph) ──
		var tabGeneral = new TabPage("通用文本 (h/pdesc/ph)");
		tabGeneral.BackColor = Color.FromArgb(45, 45, 45);
		tabGeneral.ForeColor = Color.White;
		tabTextEdit.TabPages.Add(tabGeneral);

		int gY = 6;
		var panelGeneral = new Panel
		{
			Dock = DockStyle.Fill,
			BackColor = Color.FromArgb(45, 45, 45),
			AutoScroll = true,
			Padding = new Padding(4, 4, 4, 4)
		};
		tabGeneral.Controls.Add(panelGeneral);

		panelGeneral.Controls.Add(new Label
		{
			Text = "h (模板描述):",
			Location = new Point(0, gY),
			AutoSize = true,
			ForeColor = Color.FromArgb(180, 180, 180)
		});
		panelGeneral.Controls.Add(new Label
		{
			Text = "#mpCon #damage #time 等占位符。与 h1/h2 互斥",
			Location = new Point(96, gY),
			AutoSize = true,
			ForeColor = Color.FromArgb(100, 100, 100),
			Font = new Font("Microsoft YaHei UI", 7.5f)
		});
		gY += 18;
		txtHTemplate = new TextBox
		{
			Location = new Point(0, gY),
			Size = new Size(100, 36),
			Multiline = true,
			WordWrap = true,
			BackColor = Color.FromArgb(35, 35, 35),
			ForeColor = Color.FromArgb(220, 220, 220),
			BorderStyle = BorderStyle.FixedSingle,
			Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right
		};
		SetControlTip(txtHTemplate, "模板描述文本。例: 消耗MP#mpCon，在#time秒内物理防御力增加#pdd\n常见占位符: #mpCon #hpCon #time #damage #damage% #pdd #mdd #pad #mad #acc #eva #speed #jump #dot #dotTime");
		panelGeneral.Controls.Add(txtHTemplate);
		gY += 40;

		panelGeneral.Controls.Add(new Label
		{
			Text = "pdesc:",
			Location = new Point(0, gY),
			AutoSize = true,
			ForeColor = Color.FromArgb(180, 180, 180)
		});
		panelGeneral.Controls.Add(new Label
		{
			Text = "PvP / 特殊模式备用描述",
			Location = new Point(48, gY),
			AutoSize = true,
			ForeColor = Color.FromArgb(100, 100, 100),
			Font = new Font("Microsoft YaHei UI", 7.5f)
		});
		gY += 18;
		txtPDesc = new TextBox
		{
			Location = new Point(0, gY),
			Size = new Size(100, 20),
			BackColor = Color.FromArgb(35, 35, 35),
			ForeColor = Color.FromArgb(220, 220, 220),
			BorderStyle = BorderStyle.FixedSingle,
			Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right
		};
		SetControlTip(txtPDesc, "pdesc: 预览/PvP 模式下的备用技能描述文本");
		panelGeneral.Controls.Add(txtPDesc);
		gY += 24;

		panelGeneral.Controls.Add(new Label
		{
			Text = "ph:",
			Location = new Point(0, gY),
			AutoSize = true,
			ForeColor = Color.FromArgb(180, 180, 180)
		});
		panelGeneral.Controls.Add(new Label
		{
			Text = "与 pdesc 配套，可含占位符",
			Location = new Point(30, gY),
			AutoSize = true,
			ForeColor = Color.FromArgb(100, 100, 100),
			Font = new Font("Microsoft YaHei UI", 7.5f)
		});
		gY += 18;
		txtPh = new TextBox
		{
			Location = new Point(0, gY),
			Size = new Size(100, 20),
			BackColor = Color.FromArgb(35, 35, 35),
			ForeColor = Color.FromArgb(220, 220, 220),
			BorderStyle = BorderStyle.FixedSingle,
			Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right
		};
		SetControlTip(txtPh, "ph: 预览/PvP 模式下的详情模板，与 pdesc 配套。可用同类占位符");
		panelGeneral.Controls.Add(txtPh);

		// Size textboxes to fill panel width on first layout
		panelGeneral.Layout += (s, e) =>
		{
			int w = panelGeneral.ClientSize.Width - 8;
			if (w > 50)
			{
				txtHTemplate.Width = w;
				txtPDesc.Width = w;
				txtPh.Width = w;
			}
		};

		// ── Tab 2: 等级描述 (h1/h2/h3...) ──
		var tabHLevels = new TabPage("等级描述 (h1/h2/h3...)");
		tabHLevels.BackColor = Color.FromArgb(45, 45, 45);
		tabHLevels.ForeColor = Color.White;
		tabTextEdit.TabPages.Add(tabHLevels);

		var lblHLevelsHint = new Label
		{
			Text = "每个等级一条独立说明文本 h1/h2/h3...（与通用 h 互斥）",
			Dock = DockStyle.Top,
			Height = 20,
			Padding = new Padding(4, 4, 0, 0),
			ForeColor = Color.FromArgb(100, 100, 100),
			Font = new Font("Microsoft YaHei UI", 7.5f)
		};

		dgvHLevels = new DataGridView
		{
			Dock = DockStyle.Fill,
			ReadOnly = false,
			AllowUserToAddRows = true,
			AllowUserToDeleteRows = true,
			RowHeadersVisible = false,
			AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill,
			BackgroundColor = Color.FromArgb(45, 45, 45),
			ForeColor = Color.White,
			GridColor = Color.FromArgb(60, 60, 60)
		};
		dgvHLevels.Columns.Add("Key", "键 (h1/h2...)");
		dgvHLevels.Columns.Add("Value", "文本内容");
		dgvHLevels.Columns["Key"].FillWeight = 18;
		dgvHLevels.Columns["Value"].FillWeight = 82;
		dgvHLevels.DefaultCellStyle.BackColor = Color.FromArgb(45, 45, 45);
		dgvHLevels.DefaultCellStyle.ForeColor = Color.White;
		dgvHLevels.ColumnHeadersDefaultCellStyle.BackColor = Color.FromArgb(60, 60, 60);
		dgvHLevels.ColumnHeadersDefaultCellStyle.ForeColor = Color.White;
		dgvHLevels.EnableHeadersVisualStyles = false;

		var hLevelsMenu = new ContextMenuStrip();
		hLevelsMenu.Items.Add("添加一行 (下一个 h#)", null, HLevelsMenu_AddRow);
		hLevelsMenu.Items.Add("批量添加 (按技能等级数)", null, HLevelsMenu_BatchAdd);
		hLevelsMenu.Items.Add(new ToolStripSeparator());
		hLevelsMenu.Items.Add("删除选中行", null, HLevelsMenu_DeleteRow);
		hLevelsMenu.Items.Add("清空全部", null, HLevelsMenu_ClearAll);
		hLevelsMenu.Items.Add(new ToolStripSeparator());
		hLevelsMenu.Items.Add("从通用h模板复制到所有等级", null, HLevelsMenu_CopyFromTemplate);
		dgvHLevels.ContextMenuStrip = hLevelsMenu;
		// Dock order: Fill first, then Top (WinForms docks last-added first)
		tabHLevels.Controls.Add(dgvHLevels);
		tabHLevels.Controls.Add(lblHLevelsHint);

		void LayoutRightEditorFill()
		{
			if (panel == null || treeSkillData == null || dgvLevelParams == null || _lblParamType == null || labelNodeTree == null)
			{
				return;
			}
			int clientWidth = panel.ClientSize.Width;
			int clientHeight = panel.ClientSize.Height;
			if (clientWidth <= 0 || clientHeight <= 0)
			{
				return;
			}
			int rightPadding = (panel.VerticalScroll.Visible ? SystemInformation.VerticalScrollBarWidth : 0) + 10;
			int editorX = num3;
			int editorWidth = Math.Max(220, clientWidth - editorX - rightPadding);
			int titleY = 43;
			int treeTop = titleY + 20;
			int bottom = Math.Max(treeTop + 420, clientHeight - 8);
			int paramLabelHeight = Math.Max(_lblParamType.Height, _lblParamType.PreferredHeight);
			int textLabelHeight = Math.Max(lblTextMode.Height, lblTextMode.PreferredHeight);
			int minTreeHeight = 100;
			int minParamHeight = 100;
			int minTextHeight = 140;
			int gapTreeToLabel = 8;
			int gapLabelToGrid = 4;
			int gapParamToTextLabel = 8;
			int gapTextLabelToTab = 4;
			int fixedGaps = gapTreeToLabel + paramLabelHeight + gapLabelToGrid
				+ gapParamToTextLabel + textLabelHeight + gapTextLabelToTab;
			int available = bottom - treeTop - fixedGaps;
			if (available < minTreeHeight + minParamHeight + minTextHeight)
			{
				available = minTreeHeight + minParamHeight + minTextHeight;
			}
			// Split: tree ~30%, params ~35%, text ~35%
			int treeHeight = Math.Max(minTreeHeight, available * 30 / 100);
			int paramHeight = Math.Max(minParamHeight, available * 35 / 100);
			int textHeight = Math.Max(minTextHeight, available - treeHeight - paramHeight);
			labelNodeTree.Location = new Point(editorX, titleY);
			treeSkillData.Location = new Point(editorX, treeTop);
			treeSkillData.Size = new Size(editorWidth, treeHeight);
			_lblParamType.Location = new Point(editorX, treeSkillData.Bottom + gapTreeToLabel);
			dgvLevelParams.Location = new Point(editorX, _lblParamType.Bottom + gapLabelToGrid);
			dgvLevelParams.Size = new Size(editorWidth, paramHeight);
			lblTextMode.Location = new Point(editorX, dgvLevelParams.Bottom + gapParamToTextLabel);
			tabTextEdit.Location = new Point(editorX, lblTextMode.Bottom + gapTextLabelToTab);
			tabTextEdit.Size = new Size(editorWidth, textHeight);
		}
		panel.Resize += delegate
		{
			LayoutRightEditorFill();
		};
		panel.HandleCreated += delegate
		{
			LayoutRightEditorFill();
		};
		LayoutRightEditorFill();
		btnAddToList = new Button
		{
			Text = "添加到列表 >>",
			Width = 110,
			Height = 28
		};
		btnAddToList.Click += BtnAddToList_Click;
		btnUpdateSelected = new Button
		{
			Text = "更新选中",
			Width = 80,
			Height = 28
		};
		btnUpdateSelected.Click += BtnUpdateSelected_Click;
		btnRemoveFromList = new Button
		{
			Text = "从列表移除",
			Width = 90,
			Height = 28
		};
		btnRemoveFromList.Click += BtnRemoveFromList_Click;
		btnClearList = new Button
		{
			Text = "清空列表",
			Width = 80,
			Height = 28
		};
		btnClearList.Click += BtnClearList_Click;
		btnUndo = new Button
		{
			Text = "回退 (Ctrl+Z)",
			Width = 95,
			Height = 28
		};
		btnUndo.Click += BtnUndo_Click;
		btnRedo = new Button
		{
			Text = "恢复 (Ctrl+Y)",
			Width = 95,
			Height = 28
		};
		btnRedo.Click += BtnRedo_Click;
		FlowLayoutPanel flowLayoutPanel = new FlowLayoutPanel
		{
			Dock = DockStyle.Fill,
			WrapContents = false,
			AutoSize = true
		};
		flowLayoutPanel.Controls.AddRange(btnAddToList, btnUpdateSelected, btnRemoveFromList, btnClearList, btnUndo, btnRedo);
		lvSkills = new ListView
		{
			Dock = DockStyle.Fill,
			View = View.Details,
			FullRowSelect = true,
			GridLines = true,
			MultiSelect = true
		};
		lvSkills.Columns.Add("技能ID", 80);
		lvSkills.Columns.Add("名称", 120);
		lvSkills.Columns.Add("分类", 60);
		lvSkills.Columns.Add("释放类型", 90);
		lvSkills.Columns.Add("代理技能", 75);
		lvSkills.Columns.Add("最大等级", 55);
		lvSkills.Columns.Add("SP", 35);
		lvSkills.Columns.Add("来源", 65);
		lvSkills.Columns.Add("操作", 55);
		lvSkills.Columns.Add("状态", 60);
		lvSkills.MouseDoubleClick += LvSkills_DoubleClick;
		ContextMenuStrip contextMenuStrip4 = new ContextMenuStrip();
		contextMenuStrip4.Items.Add("编辑", null, delegate
		{
			if (lvSkills.SelectedIndices.Count > 0)
			{
				LvSkills_DoubleClick(lvSkills, new MouseEventArgs(MouseButtons.Left, 2, 0, 0, 0));
			}
		});
		contextMenuStrip4.Items.Add("删除", null, delegate(object s2, EventArgs e2)
		{
			BtnRemoveFromList_Click(s2, e2);
		});
		contextMenuStrip4.Items.Add(new ToolStripSeparator());
		contextMenuStrip4.Items.Add("从 .img 重新加载", null, delegate
		{
			ReloadSelectedFromImg();
		});
		lvSkills.ContextMenuStrip = contextMenuStrip4;
		btnExecuteAdd = new Button
		{
			Text = "执行新增/删除",
			Width = 110,
			Height = 30,
			BackColor = Color.FromArgb(50, 120, 50),
			ForeColor = Color.White
		};
		btnExecuteAdd.Click += BtnExecuteAdd_Click;
		/*btnDryRun = new Button
		{
			Text = "预览（演练）",
			Width = 110,
			Height = 30
		};
		btnDryRun.Click += BtnDryRun_Click;*/
		chkSkipImg = new CheckBox
		{
			Text = "跳过服务端XML",
			AutoSize = true
		};
		FlowLayoutPanel flowLayoutPanel2 = new FlowLayoutPanel
		{
			Dock = DockStyle.Fill,
			WrapContents = false,
			AutoSize = true
		};
		flowLayoutPanel2.Controls.AddRange(btnExecuteAdd,  chkSkipImg);
		TableLayoutPanel tableLayoutPanel = new TableLayoutPanel
		{
			Dock = DockStyle.Fill,
			ColumnCount = 1,
			RowCount = 3
		};
		tableLayoutPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
		tableLayoutPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
		tableLayoutPanel.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
		tableLayoutPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
		tableLayoutPanel.Controls.Add(flowLayoutPanel, 0, 0);
		tableLayoutPanel.Controls.Add(lvSkills, 0, 1);
		tableLayoutPanel.Controls.Add(flowLayoutPanel2, 0, 2);
		panel2.Controls.Add(tableLayoutPanel);
	}

	private void BuildSkillLibraryPanel(Control host)
	{
		if (host == null)
		{
			return;
		}
		Panel panel = new Panel
		{
			Dock = DockStyle.Fill,
			Padding = new Padding(6, 6, 6, 6)
		};
		host.Controls.Add(panel);
		TableLayoutPanel tableLayoutPanel = new TableLayoutPanel
		{
			Dock = DockStyle.Fill,
			ColumnCount = 1,
			RowCount = 5
		};
		tableLayoutPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
		tableLayoutPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 20f));
		tableLayoutPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 24f));
		tableLayoutPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 24f));
		tableLayoutPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 18f));
		tableLayoutPanel.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
		panel.Controls.Add(tableLayoutPanel);
		Label label = new Label
		{
			Text = "技能库（双击载入）",
			Dock = DockStyle.Fill,
			Height = 20,
			TextAlign = ContentAlignment.MiddleLeft,
			ForeColor = Color.FromArgb(80, 160, 220)
		};
		try
		{
			label.Font = new Font("Microsoft YaHei UI", 8f);
		}
		catch
		{
		}
		txtSkillLibrarySearch = new TextBox
		{
			Dock = DockStyle.Fill
		};
		try
		{
			txtSkillLibrarySearch.Font = new Font("Microsoft YaHei UI", 8f);
		}
		catch
		{
		}
		try
		{
			txtSkillLibrarySearch.PlaceholderText = "搜索 名称 / 描述 / 技能ID";
		}
		catch
		{
		}
		txtSkillLibrarySearch.TextChanged += delegate
		{
			RefreshSkillLibraryList();
		};
		FlowLayoutPanel flowLayoutPanel = new FlowLayoutPanel
		{
			Dock = DockStyle.Fill,
			AutoSize = false,
			WrapContents = false,
			FlowDirection = FlowDirection.LeftToRight,
			Margin = new Padding(0)
		};
		Label label2 = new Label
		{
			Text = "过滤:",
			AutoSize = true,
			Margin = new Padding(0, 4, 6, 0)
		};
		try
		{
			label2.Font = new Font("Microsoft YaHei UI", 8f);
		}
		catch
		{
		}
		chkSkillFilterName = new CheckBox
		{
			Text = "名字",
			AutoSize = true,
			Checked = true,
			Margin = new Padding(0, 2, 8, 0)
		};
		chkSkillFilterId = new CheckBox
		{
			Text = "ID",
			AutoSize = true,
			Checked = true,
			Margin = new Padding(0, 2, 8, 0)
		};
		chkSkillFilterDesc = new CheckBox
		{
			Text = "描述",
			AutoSize = true,
			Checked = true,
			Margin = new Padding(0, 2, 0, 0)
		};
		try
		{
			chkSkillFilterName.Font = new Font("Microsoft YaHei UI", 8f);
			chkSkillFilterId.Font = new Font("Microsoft YaHei UI", 8f);
			chkSkillFilterDesc.Font = new Font("Microsoft YaHei UI", 8f);
		}
		catch
		{
		}
		chkSkillFilterName.CheckedChanged += delegate
		{
			RefreshSkillLibraryList();
		};
		chkSkillFilterId.CheckedChanged += delegate
		{
			RefreshSkillLibraryList();
		};
		chkSkillFilterDesc.CheckedChanged += delegate
		{
			RefreshSkillLibraryList();
		};
		flowLayoutPanel.Controls.Add(label2);
		flowLayoutPanel.Controls.Add(chkSkillFilterName);
		flowLayoutPanel.Controls.Add(chkSkillFilterId);
		flowLayoutPanel.Controls.Add(chkSkillFilterDesc);
		lblSkillLibraryStatus = new Label
		{
			Text = "技能库: 0 / 0",
			Dock = DockStyle.Fill,
			Height = 18,
			TextAlign = ContentAlignment.MiddleLeft,
			ForeColor = Color.Gray
		};
		try
		{
			lblSkillLibraryStatus.Font = new Font("Microsoft YaHei UI", 8f);
		}
		catch
		{
		}
		lvSkillLibrary = new ListView
		{
			Dock = DockStyle.Fill,
			View = View.Details,
			FullRowSelect = true,
			GridLines = true,
			HideSelection = false
		};
		try
		{
			lvSkillLibrary.Font = new Font("Microsoft YaHei UI", 8f);
		}
		catch
		{
		}
		lvSkillLibrary.Columns.Add("技能名-ID", 140);
		lvSkillLibrary.Columns.Add("描述", 150);
		lvSkillLibrary.DoubleClick += LvSkillLibrary_DoubleClick;
		lvSkillLibrary.KeyDown += LvSkillLibrary_KeyDown;
		tableLayoutPanel.Controls.Add(label, 0, 0);
		tableLayoutPanel.Controls.Add(txtSkillLibrarySearch, 0, 1);
		tableLayoutPanel.Controls.Add(flowLayoutPanel, 0, 2);
		tableLayoutPanel.Controls.Add(lblSkillLibraryStatus, 0, 3);
		tableLayoutPanel.Controls.Add(lvSkillLibrary, 0, 4);
	}

	private void LoadSkillLibrary(bool forceReload = false)
	{
		if (!forceReload && _skillLibraryItems != null && _skillLibraryItems.Count > 0)
		{
			RefreshSkillLibraryList();
			return;
		}
		if (_skillLibraryLoading)
		{
			return;
		}
		_skillLibraryLoading = true;
		RunInThread(delegate
		{
			try
			{
				var list = new List<SkillLibraryItem>();
				var seenSkillIds = new HashSet<int>();
				using (WzImgLoader wzImgLoader = new WzImgLoader())
				{
					Dictionary<int, Tuple<string, string>> dictionary = wzImgLoader.BuildStringSkillIndex();
					List<int> list2 = EnumerateJobIdsFromGameSkillRoot();
					foreach (int item in list2)
					{
						List<int> list3;
						try
						{
							list3 = wzImgLoader.ListSkillIds(item);
						}
						catch
						{
							continue;
						}
						foreach (int item2 in list3)
						{
							if (!seenSkillIds.Add(item2))
							{
								continue;
							}
							dictionary.TryGetValue(item2, out Tuple<string, string> value);
							string name = value?.Item1 ?? "";
							string desc = value?.Item2 ?? "";
							SkillLibraryItem skillLibraryItem = new SkillLibraryItem
							{
								SkillId = item2,
								JobId = item,
								Name = name,
								Desc = desc
							};
							skillLibraryItem.SearchText = ((skillLibraryItem.SkillId.ToString() + " " + (skillLibraryItem.Name ?? "") + " " + (skillLibraryItem.Desc ?? "")).Trim().ToLowerInvariant());
							list.Add(skillLibraryItem);
						}
					}
				}
				list.Sort((SkillLibraryItem a, SkillLibraryItem b) => a.SkillId.CompareTo(b.SkillId));
				SafeInvoke(delegate
				{
					_skillLibraryItems = list;
					RefreshSkillLibraryList();
				});
			}
			catch (Exception ex)
			{
				SafeInvoke(delegate
				{
					_skillLibraryItems = new List<SkillLibraryItem>();
					RefreshSkillLibraryList();
				});
				Console.WriteLine("[技能库] 加载失败: " + ex.Message);
			}
			finally
			{
				_skillLibraryLoading = false;
			}
		});
	}

	private static List<int> EnumerateJobIdsFromGameSkillRoot()
	{
		List<int> list = new List<int>();
		string gameDataRoot = PathConfig.GameDataRoot;
		if (string.IsNullOrWhiteSpace(gameDataRoot) || !Directory.Exists(gameDataRoot))
		{
			return list;
		}
		try
		{
			string[] files = Directory.GetFiles(gameDataRoot, "*.img", SearchOption.TopDirectoryOnly);
			foreach (string text in files)
			{
				string fileNameWithoutExtension = Path.GetFileNameWithoutExtension(text);
				if (int.TryParse(fileNameWithoutExtension, out var result) && result >= 0)
				{
					list.Add(result);
				}
			}
			list.Sort();
		}
		catch
		{
		}
		return list;
	}

	private void RefreshSkillLibraryList()
	{
		if (lvSkillLibrary == null)
		{
			return;
		}
		string text = txtSkillLibrarySearch?.Text?.Trim().ToLowerInvariant() ?? "";
		IEnumerable<SkillLibraryItem> enumerable = _skillLibraryItems ?? Enumerable.Empty<SkillLibraryItem>();
		if (!string.IsNullOrEmpty(text))
		{
			bool flag = chkSkillFilterName?.Checked != false;
			bool flag2 = chkSkillFilterId?.Checked != false;
			bool flag3 = chkSkillFilterDesc?.Checked != false;
			if (!flag && !flag2 && !flag3)
			{
				flag = true;
				flag2 = true;
				flag3 = true;
			}
			enumerable = enumerable.Where(delegate(SkillLibraryItem x)
			{
				if (x == null)
				{
					return false;
				}
				if (flag2 && x.SkillId.ToString().Contains(text))
				{
					return true;
				}
				if (flag && !string.IsNullOrEmpty(x.Name) && x.Name.ToLowerInvariant().Contains(text))
				{
					return true;
				}
				if (flag3 && !string.IsNullOrEmpty(x.Desc) && x.Desc.ToLowerInvariant().Contains(text))
				{
					return true;
				}
				return false;
			});
		}
		List<SkillLibraryItem> list = enumerable.Where((SkillLibraryItem x) => x != null).ToList();
		lvSkillLibrary.BeginUpdate();
		lvSkillLibrary.Items.Clear();
		foreach (SkillLibraryItem item in list)
		{
			ListViewItem listViewItem = new ListViewItem(item.Display);
			listViewItem.SubItems.Add(item.Desc ?? "");
			listViewItem.Tag = item;
			lvSkillLibrary.Items.Add(listViewItem);
		}
		lvSkillLibrary.EndUpdate();
		if (lblSkillLibraryStatus != null)
		{
			lblSkillLibraryStatus.Text = $"技能库: {list.Count} / {(_skillLibraryItems?.Count ?? 0)}";
		}
	}

	private void LvSkillLibrary_DoubleClick(object sender, EventArgs e)
	{
		LoadSelectedSkillLibraryItem();
	}

	private void LvSkillLibrary_KeyDown(object sender, KeyEventArgs e)
	{
		if (e.KeyCode == Keys.Return)
		{
			LoadSelectedSkillLibraryItem();
			e.SuppressKeyPress = true;
		}
	}

	private void LoadSelectedSkillLibraryItem()
	{
		if (lvSkillLibrary == null || lvSkillLibrary.SelectedItems.Count == 0)
		{
			return;
		}
		if (!(lvSkillLibrary.SelectedItems[0].Tag is SkillLibraryItem skillLibraryItem) || skillLibraryItem.SkillId <= 0)
		{
			return;
		}
		txtSkillIdInput.Text = skillLibraryItem.SkillId.ToString();
		DoLoadSkill(skillLibraryItem.JobId >= 0 ? skillLibraryItem.JobId : null);
	}

	private void BuildTab2_Management()
	{
		TabPage tabPage = new TabPage("管理");
		tabMain.TabPages.Add(tabPage);
		Panel panel = new Panel
		{
			Dock = DockStyle.Fill,
			AutoScroll = true
		};
		tabPage.Controls.Add(panel);
		int num = 8;
		GroupBox groupBox = new GroupBox
		{
			Text = "验证",
			Location = new Point(8, num),
			Size = new Size(860, 60)
		};
		groupBox.Controls.Add(new Label
		{
			Text = "技能ID:",
			Location = new Point(10, 23),
			AutoSize = true
		});
		txtVerifyId = new TextBox
		{
			Location = new Point(70, 20),
			Width = 100
		};
		groupBox.Controls.Add(txtVerifyId);
		Button button = new Button
		{
			Text = "验证单个",
			Location = new Point(180, 19),
			Width = 80
		};
		button.Click += delegate
		{
			if (int.TryParse(txtVerifyId.Text, out var id) && id > 0)
			{
				RunInThread(delegate
				{
					VerifyGenerator.Verify(id);
				});
			}
			else
			{
				MessageBox.Show("请输入有效的技能ID", "提示");
			}
		};
		groupBox.Controls.Add(button);
		Button button2 = new Button
		{
			Text = "验证全部",
			Location = new Point(268, 19),
			Width = 80
		};
		button2.Click += delegate
		{
			RunInThread(delegate
			{
				VerifyGenerator.VerifyAll();
			});
		};
		groupBox.Controls.Add(button2);
		panel.Controls.Add(groupBox);
		num += 68;
		GroupBox groupBox2 = new GroupBox
		{
			Text = "删除",
			Location = new Point(8, num),
			Size = new Size(860, 60)
		};
		groupBox2.Controls.Add(new Label
		{
			Text = "技能ID:",
			Location = new Point(10, 23),
			AutoSize = true
		});
		txtRemoveId = new TextBox
		{
			Location = new Point(70, 20),
			Width = 100
		};
		groupBox2.Controls.Add(txtRemoveId);
		chkRemoveDryRun = new CheckBox
		{
			Text = "演练",
			Location = new Point(180, 22),
			AutoSize = true
		};
		groupBox2.Controls.Add(chkRemoveDryRun);
		Button button3 = new Button
		{
			Text = "删除技能",
			Location = new Point(270, 19),
			Width = 80,
			BackColor = Color.FromArgb(180, 50, 50),
			ForeColor = Color.White
		};
		button3.Click += delegate
		{
			if (!int.TryParse(txtRemoveId.Text, out var id) || id <= 0)
			{
				MessageBox.Show("请输入有效的技能ID", "提示");
			}
			else
			{
				bool dry = chkRemoveDryRun.Checked;
				if (dry || MessageBox.Show("删除技能 " + id + " 于所有目标文件中吗？", "确认删除", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation) == DialogResult.Yes)
				{
					RunInThread(delegate
					{
						SkillRemover.Remove(id, dry);
					});
				}
			}
		};
		groupBox2.Controls.Add(button3);
		panel.Controls.Add(groupBox2);
		num += 68;
		GroupBox groupBox3 = new GroupBox
		{
			Text = "快速导入 / 导出",
			Location = new Point(8, num),
			Size = new Size(860, 60)
		};
		Button button4 = new Button
		{
			Text = "从JSON导入...",
			Location = new Point(10, 22),
			Width = 120
		};
		button4.Click += BtnImportJson_Click;
		groupBox3.Controls.Add(button4);
		Button button5 = new Button
		{
			Text = "导出当前列表...",
			Location = new Point(140, 22),
			Width = 120
		};
		button5.Click += BtnExportJson_Click;
		groupBox3.Controls.Add(button5);
		panel.Controls.Add(groupBox3);
	}

	private void BuildTab3_Settings()
	{
		TabPage tabPage = new TabPage("设置");
		tabMain.TabPages.Add(tabPage);
		Panel panel = new Panel
		{
			Dock = DockStyle.Fill,
			AutoScroll = true
		};
		tabPage.Controls.Add(panel);
		int y = 12;
		txtServerRootDir = AddSettingRow(panel, ref y, "服务端目录根路径:", PathConfig.ServerRootDir, browse: true);
		txtGameDataBaseDir = AddSettingRow(panel, ref y, "游戏Data目录根路径:", PathConfig.GameDataBaseDir, browse: true);
		txtOutputDir = AddSettingRow(panel, ref y, "输出目录:", PathConfig.OutputDir, browse: true);
		txtConfigDataDir = AddSettingRow(panel, ref y, "配置数据目录:", PathConfig.ConfigDataDir, browse: true);

		Label lblAutoDerived = new Label
		{
			Location = new Point(10, y),
			Size = new Size(980, 185),
			BorderStyle = BorderStyle.FixedSingle,
			Padding = new Padding(8, 6, 8, 6),
			AutoEllipsis = true
		};
		panel.Controls.Add(lblAutoDerived);
		y += lblAutoDerived.Height + 30;

		void RefreshAutoDerivedPreview()
		{
			string serverRoot = (txtServerRootDir?.Text ?? "").Trim().TrimEnd('\\');
			string gameRoot = (txtGameDataBaseDir?.Text ?? "").Trim().TrimEnd('\\');
			if (string.IsNullOrWhiteSpace(serverRoot))
				serverRoot = PathConfig.ServerRootDir;
			if (string.IsNullOrWhiteSpace(gameRoot))
				gameRoot = PathConfig.GameDataBaseDir;

			string defaultOutput = Path.Combine(serverRoot, "output");
			lblAutoDerived.Text =
				"自动派生路径预览：" + Environment.NewLine +
				$"服务端: {Path.Combine(serverRoot, "wz", "Skill.wz")}" + Environment.NewLine +
				$"服务端字符串: {Path.Combine(serverRoot, "wz", "String.wz", "Skill.img.xml")}" + Environment.NewLine +
				$"服务端超技配置: {Path.Combine(serverRoot, "super_skills_server.json")}" + Environment.NewLine +
				$"服务端坐骑动作XML目录: {Path.Combine(serverRoot, "wz", "Character.wz", "TamingMob")}" + Environment.NewLine +
				$"服务端坐骑参数XML目录: {Path.Combine(serverRoot, "wz", "TamingMob.wz")}" + Environment.NewLine +
				$"游戏技能目录: {Path.Combine(gameRoot, "Skill")}" + Environment.NewLine +
				$"游戏坐骑动作目录: {Path.Combine(gameRoot, "Character", "TamingMob")}" + Environment.NewLine +
				$"游戏坐骑参数目录: {Path.Combine(gameRoot, "TamingMob")}" + Environment.NewLine +
				$"默认输出目录: {defaultOutput}";
		}

		txtServerRootDir.TextChanged += delegate { RefreshAutoDerivedPreview(); };
		txtGameDataBaseDir.TextChanged += delegate { RefreshAutoDerivedPreview(); };
		RefreshAutoDerivedPreview();

		panel.Controls.Add(new Label
		{
			Text = "默认载体技能ID:",
			Location = new Point(10, y + 3),
			AutoSize = true
		});
		txtDefaultCarrierId = new TextBox
		{
			Location = new Point(140, y),
			Width = 100,
			Text = PathConfig.DefaultSuperSpCarrierSkillId.ToString()
		};
		WireCarrierSkillIdEditor(txtDefaultCarrierId);
		panel.Controls.Add(txtDefaultCarrierId);
		y += 36;
		Button button = new Button
		{
			Text = "保存设置",
			Location = new Point(10, y),
			Width = 90
		};
		button.Click += BtnSaveSettings_Click;
		panel.Controls.Add(button);
		Button button2 = new Button
		{
			Text = "恢复默认",
			Location = new Point(110, y),
			Width = 90
		};
		button2.Click += BtnResetSettings_Click;
		panel.Controls.Add(button2);
	}

	private void BuildTab4_MountEditor()
	{
		TabPage tabPage = new TabPage("坐骑编辑");
		tabMain.TabPages.Add(tabPage);

		TableLayoutPanel root = new TableLayoutPanel
		{
			Dock = DockStyle.Fill,
			ColumnCount = 1,
			RowCount = 2
		};
		root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
		root.RowStyles.Add(new RowStyle(SizeType.Absolute, 72f));
		root.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
		tabPage.Controls.Add(root);

		Panel top = new Panel { Dock = DockStyle.Fill };
		root.Controls.Add(top, 0, 0);

		top.Controls.Add(new Label { Text = "坐骑ItemId:", Location = new Point(10, 11), AutoSize = true });
		txtMountEditorItemId = new TextBox { Location = new Point(84, 8), Width = 86 };
		txtMountEditorItemId.KeyDown += delegate(object sender, KeyEventArgs e)
		{
			if (e.KeyCode == Keys.Return || e.KeyCode == Keys.Enter)
			{
				e.Handled = true;
				e.SuppressKeyPress = true;
				BtnMountLoad_Click(sender, EventArgs.Empty);
			}
		};
		top.Controls.Add(txtMountEditorItemId);

		// hidden compatibility fields (advanced values are auto-resolved now)
		txtMountEditorSourceItemId = new TextBox { Visible = false };
		txtMountEditorDataId = new TextBox { Visible = false };

		lblMountEditorDataBinding = new Label
		{
			Text = "DataID: 自动读取",
			Location = new Point(174, 11),
			AutoSize = true,
			ForeColor = Color.Gray
		};
		top.Controls.Add(lblMountEditorDataBinding);

		btnMountLoad = new Button { Text = "加载/编辑", Location = new Point(300, 6), Width = 82 };
		btnMountLoad.Click += BtnMountLoad_Click;
		top.Controls.Add(btnMountLoad);

		btnMountClone = new Button { Text = "按ID克隆...", Location = new Point(388, 6), Width = 88 };
		btnMountClone.Click += BtnMountClone_Click;
		top.Controls.Add(btnMountClone);

		btnMountAddNode = new Button { Text = "新增动作节点", Location = new Point(482, 6), Width = 90 };
		btnMountAddNode.Click += BtnMountAddNode_Click;
		top.Controls.Add(btnMountAddNode);

		btnMountRemoveNode = new Button { Text = "删除动作节点", Location = new Point(578, 6), Width = 90 };
		btnMountRemoveNode.Click += BtnMountRemoveNode_Click;
		top.Controls.Add(btnMountRemoveNode);

		btnMountSaveAction = new Button { Text = "保存动作IMG", Location = new Point(10, 38), Width = 92 };
		btnMountSaveAction.Click += BtnMountSaveAction_Click;
		top.Controls.Add(btnMountSaveAction);

		btnMountSaveData = new Button { Text = "保存参数IMG", Location = new Point(108, 38), Width = 92 };
		btnMountSaveData.Click += BtnMountSaveData_Click;
		top.Controls.Add(btnMountSaveData);

		btnMountSyncXml = new Button { Text = "同步服务端XML", Location = new Point(206, 38), Width = 102 };
		btnMountSyncXml.Click += BtnMountSyncXml_Click;
		top.Controls.Add(btnMountSyncXml);

		btnMountSaveAll = new Button
		{
			Text = "全部保存(动作+参数+XML)",
			Location = new Point(314, 38),
			Width = 170,
			BackColor = Color.FromArgb(50, 120, 50),
			ForeColor = Color.White
		};
		btnMountSaveAll.Click += BtnMountSaveAll_Click;
		top.Controls.Add(btnMountSaveAll);

		btnMountApplyToSkill = new Button { Text = "写回技能编辑", Location = new Point(490, 38), Width = 102 };
		btnMountApplyToSkill.Click += BtnMountApplyToSkill_Click;
		top.Controls.Add(btnMountApplyToSkill);

		top.Controls.Add(new Label
		{
			Text = "已添加坐骑ID:",
			Location = new Point(600, 43),
			AutoSize = true
		});
		cboMountKnownIds = new ComboBox
		{
			Location = new Point(690, 40),
			Width = 190,
			DropDownStyle = ComboBoxStyle.DropDownList
		};
		top.Controls.Add(cboMountKnownIds);
		btnMountKnownLoad = new Button
		{
			Text = "载入",
			Location = new Point(885, 39),
			Width = 50
		};
		btnMountKnownLoad.Click += delegate
		{
			if (cboMountKnownIds == null || cboMountKnownIds.SelectedItem == null)
			{
				MessageBox.Show("请先选择一个坐骑ID", "提示");
				return;
			}

			int selectedId = ParseMountKnownId(cboMountKnownIds.SelectedItem.ToString());
			if (selectedId <= 0)
			{
				MessageBox.Show("所选坐骑ID无效", "提示");
				return;
			}

			txtMountEditorItemId.Text = selectedId.ToString();
			BtnMountLoad_Click(this, EventArgs.Empty);
		};
		top.Controls.Add(btnMountKnownLoad);
		btnMountKnownRefresh = new Button
		{
			Text = "刷新",
			Location = new Point(940, 39),
			Width = 50
		};
		btnMountKnownRefresh.Click += delegate
		{
			RefreshMountKnownIds();
		};
		top.Controls.Add(btnMountKnownRefresh);
		RefreshMountKnownIds();

		SplitContainer split = new SplitContainer
		{
			Dock = DockStyle.Fill,
			Orientation = Orientation.Vertical,
			SplitterDistance = 560
		};
		root.Controls.Add(split, 0, 1);

		TableLayoutPanel left = new TableLayoutPanel
		{
			Dock = DockStyle.Fill,
			ColumnCount = 1,
			RowCount = 4
		};
		left.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
		left.RowStyles.Add(new RowStyle(SizeType.Absolute, 26f));
		left.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
		left.RowStyles.Add(new RowStyle(SizeType.Absolute, 18f));
		left.RowStyles.Add(new RowStyle(SizeType.Absolute, 170f));
		split.Panel1.Controls.Add(left);

		Panel nodeBar = new Panel
		{
			Dock = DockStyle.Fill,
			Margin = new Padding(0)
		};
		nodeBar.Controls.Add(new Label
		{
			Text = "动作节点:",
			AutoSize = true,
			Location = new Point(3, 6)
		});
		cboMountActionNode = new ComboBox
		{
			Location = new Point(68, 0),
			Width = 170,
			DropDownStyle = ComboBoxStyle.DropDownList
		};
		cboMountActionNode.SelectedIndexChanged += CboMountActionNode_SelectedIndexChanged;
		nodeBar.Controls.Add(cboMountActionNode);
		left.Controls.Add(nodeBar, 0, 0);

		lvMountFrames = new ListView
		{
			Dock = DockStyle.Fill,
			View = View.Details,
			FullRowSelect = true,
			MultiSelect = true,
			GridLines = true,
			AllowDrop = true
		};
		lvMountFrames.Columns.Add("帧", 48);
		lvMountFrames.Columns.Add("尺寸", 84);
		lvMountFrames.Columns.Add("延迟", 64);
		lvMountFrames.Columns.Add("定位/参数", 320);
		lvMountFrames.SelectedIndexChanged += LvMountFrames_SelectedChanged;
		lvMountFrames.DragEnter += MountFrames_DragEnter;
		lvMountFrames.DragDrop += MountFrames_DragDrop;
		left.Controls.Add(lvMountFrames, 0, 1);

		ContextMenuStrip mountFrameMenu = new ContextMenuStrip();
		mountFrameMenu.Items.Add("添加帧", null, MountMenu_AddFrame);
		mountFrameMenu.Items.Add("替换帧图片", null, MountMenu_ReplaceFrame);
		mountFrameMenu.Items.Add("编辑延迟", null, MountMenu_EditDelay);
		mountFrameMenu.Items.Add("编辑定位/参数", null, MountMenu_EditFrameMeta);
		mountFrameMenu.Items.Add(new ToolStripSeparator());
		mountFrameMenu.Items.Add("复制选中帧", null, MountMenu_CopyFrames);
		mountFrameMenu.Items.Add("粘贴帧", null, MountMenu_PasteFrames);
		mountFrameMenu.Items.Add(new ToolStripSeparator());
		mountFrameMenu.Items.Add("删除帧", null, MountMenu_DeleteFrame);
		lvMountFrames.ContextMenuStrip = mountFrameMenu;

		Label previewTip = new Label
		{
			Text = "帧预览（拖拽图片到帧列表可快速添加）",
			ForeColor = Color.Gray,
			Dock = DockStyle.Fill,
			TextAlign = ContentAlignment.MiddleLeft
		};
		left.Controls.Add(previewTip, 0, 2);

		picMountFramePreview = new PictureBox
		{
			Dock = DockStyle.Fill,
			BorderStyle = BorderStyle.FixedSingle,
			SizeMode = PictureBoxSizeMode.Zoom,
			BackColor = Color.FromArgb(40, 40, 40)
		};
		left.Controls.Add(picMountFramePreview, 0, 3);

		TableLayoutPanel right = new TableLayoutPanel
		{
			Dock = DockStyle.Fill,
			ColumnCount = 1,
			RowCount = 2
		};
		right.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
		right.RowStyles.Add(new RowStyle(SizeType.Percent, 50f));
		right.RowStyles.Add(new RowStyle(SizeType.Percent, 50f));
		split.Panel2.Controls.Add(right);

		GroupBox gbActionInfo = new GroupBox { Text = "动作IMG info（含 tamingMob）", Dock = DockStyle.Fill };
		dgvMountActionInfo = CreateMountInfoGrid();
		WireMountInfoGridToolTips(dgvMountActionInfo);
		gbActionInfo.Controls.Add(dgvMountActionInfo);
		right.Controls.Add(gbActionInfo, 0, 0);

		GroupBox gbDataInfo = new GroupBox { Text = "参数IMG info（speed/jump/fatigue...）", Dock = DockStyle.Fill };
		dgvMountDataInfo = CreateMountInfoGrid();
		WireMountInfoGridToolTips(dgvMountDataInfo);
		gbDataInfo.Controls.Add(dgvMountDataInfo);
		right.Controls.Add(gbDataInfo, 0, 1);
	}

	private DataGridView CreateMountInfoGrid()
	{
		var grid = new DataGridView
		{
			Dock = DockStyle.Fill,
			ReadOnly = false,
			AllowUserToAddRows = true,
			AllowUserToDeleteRows = true,
			RowHeadersVisible = false,
			AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill,
			BackgroundColor = Color.FromArgb(45, 45, 45),
			ForeColor = Color.White,
			GridColor = Color.FromArgb(70, 70, 70)
		};
		grid.DefaultCellStyle.BackColor = Color.FromArgb(45, 45, 45);
		grid.ColumnHeadersDefaultCellStyle.BackColor = Color.FromArgb(60, 60, 60);
		grid.ColumnHeadersDefaultCellStyle.ForeColor = Color.White;
		grid.EnableHeadersVisualStyles = false;
		grid.Columns.Add("Param", "参数");
		grid.Columns.Add("Value", "值");
		return grid;
	}

	private void WireMountInfoGridToolTips(DataGridView grid)
	{
		if (grid == null)
			return;
		grid.ShowCellToolTips = false; // avoid duplicate native tooltip (white box)
		grid.ShowCellErrors = false;
		grid.ShowRowErrors = false;
		grid.CellMouseEnter += MountInfoGrid_CellMouseEnter;
		grid.MouseMove += MountInfoGrid_MouseMove;
		grid.MouseLeave += MountInfoGrid_MouseLeave;
		grid.MouseDown += MountInfoGrid_MouseDownSelectCell;
		ContextMenuStrip menu = new ContextMenuStrip();
		menu.Items.Add("添加参数...", null, delegate
		{
			MountInfoGrid_AddParams(grid);
		});
		menu.Items.Add("删除当前参数", null, delegate
		{
			MountInfoGrid_DeleteCurrent(grid);
		});
		menu.Items.Add("删除选中参数", null, delegate
		{
			MountInfoGrid_DeleteSelected(grid);
		});
		menu.Items.Add(new ToolStripSeparator());
		menu.Items.Add("清空参数", null, delegate
		{
			if (MessageBox.Show("确认清空当前参数列表吗？", "确认", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation) == DialogResult.Yes)
			{
				grid.Rows.Clear();
			}
		});
		grid.ContextMenuStrip = menu;
		_toolTip?.SetToolTip(grid, string.Empty);
	}

	private void MountInfoGrid_CellMouseEnter(object sender, DataGridViewCellEventArgs e)
	{
		ShowMountInfoToolTip(sender as DataGridView, e.RowIndex, e.ColumnIndex, -1, -1);
	}

	private void MountInfoGrid_MouseMove(object sender, MouseEventArgs e)
	{
		if (!(sender is DataGridView grid))
			return;
		DataGridView.HitTestInfo hitTestInfo = grid.HitTest(e.X, e.Y);
		ShowMountInfoToolTip(grid, hitTestInfo.RowIndex, hitTestInfo.ColumnIndex, e.X, e.Y);
	}

	private void MountInfoGrid_MouseLeave(object sender, EventArgs e)
	{
		if (_toolTip != null && sender is DataGridView grid)
			_toolTip.Hide(grid);
		_lastMountTipGrid = null;
		_lastMountTipRow = -2;
		_lastMountTipCol = -2;
		_lastMountTipText = "";
	}

	private void MountInfoGrid_MouseDownSelectCell(object sender, MouseEventArgs e)
	{
		if (!(sender is DataGridView grid) || e.Button != MouseButtons.Right)
			return;
		DataGridView.HitTestInfo hitTestInfo = grid.HitTest(e.X, e.Y);
		if (hitTestInfo.RowIndex >= 0 && hitTestInfo.ColumnIndex >= 0
			&& hitTestInfo.RowIndex < grid.Rows.Count && !grid.Rows[hitTestInfo.RowIndex].IsNewRow)
		{
			grid.ClearSelection();
			grid.CurrentCell = grid.Rows[hitTestInfo.RowIndex].Cells[hitTestInfo.ColumnIndex];
			grid.Rows[hitTestInfo.RowIndex].Selected = true;
		}
	}

	private void MountInfoGrid_AddParams(DataGridView grid)
	{
		if (grid == null)
			return;
		bool isActionInfo = ReferenceEquals(grid, dgvMountActionInfo);
		string[] allKeys = isActionInfo ? MountActionInfoPresetKeys : MountDataInfoPresetKeys;
		if (allKeys == null || allKeys.Length == 0)
			return;

		HashSet<string> existing = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
		foreach (DataGridViewRow row in grid.Rows)
		{
			if (row == null || row.IsNewRow)
				continue;
			string key = row.Cells[0].Value?.ToString()?.Trim();
			if (!string.IsNullOrWhiteSpace(key))
				existing.Add(key);
		}

		List<string> selected = ShowMountParamPicker(isActionInfo ? "添加动作参数" : "添加数据参数", allKeys, existing);
		if (selected == null || selected.Count == 0)
			return;

		int added = 0;
		foreach (string key in selected)
		{
			if (string.IsNullOrWhiteSpace(key) || existing.Contains(key))
				continue;
			grid.Rows.Add(key, GetMountParamDefaultValue(key, isActionInfo));
			existing.Add(key);
			added++;
		}

		if (added <= 0)
			MessageBox.Show("所选参数均已存在，未重复添加。", "提示");
	}

	private void MountInfoGrid_DeleteCurrent(DataGridView grid)
	{
		if (grid?.CurrentCell == null)
			return;
		int rowIndex = grid.CurrentCell.RowIndex;
		if (rowIndex < 0 || rowIndex >= grid.Rows.Count || grid.Rows[rowIndex].IsNewRow)
			return;
		string key = grid.Rows[rowIndex].Cells[0].Value?.ToString() ?? "(空)";
		if (MessageBox.Show($"确认删除参数 \"{key}\" 吗？", "确认删除", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation) != DialogResult.Yes)
			return;
		grid.Rows.RemoveAt(rowIndex);
	}

	private void MountInfoGrid_DeleteSelected(DataGridView grid)
	{
		if (grid == null || grid.SelectedRows == null || grid.SelectedRows.Count == 0)
		{
			MountInfoGrid_DeleteCurrent(grid);
			return;
		}

		List<int> rowIndexes = new List<int>();
		foreach (DataGridViewRow row in grid.SelectedRows)
		{
			if (row != null && !row.IsNewRow && row.Index >= 0)
				rowIndexes.Add(row.Index);
		}
		if (rowIndexes.Count == 0)
			return;
		if (MessageBox.Show($"确认删除选中的 {rowIndexes.Count} 个参数吗？", "确认删除", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation) != DialogResult.Yes)
			return;
		rowIndexes.Sort();
		rowIndexes.Reverse();
		foreach (int idx in rowIndexes)
		{
			if (idx >= 0 && idx < grid.Rows.Count && !grid.Rows[idx].IsNewRow)
				grid.Rows.RemoveAt(idx);
		}
	}

	private static string GetMountParamDefaultValue(string key, bool isActionInfo)
	{
		string k = (key ?? "").Trim();
		if (string.IsNullOrEmpty(k))
			return "";
		if (isActionInfo)
		{
			if (string.Equals(k, "tamingMob", StringComparison.OrdinalIgnoreCase))
				return "1";
			if (k.StartsWith("passengerNavel", StringComparison.OrdinalIgnoreCase))
				return "0";
			return "0";
		}

		switch (k.ToLowerInvariant())
		{
		case "speed":
			return "140";
		case "jump":
			return "120";
		case "fatigue":
			return "1";
		case "fs":
		case "swim":
		case "continentMove":
		case "userSpeed":
		case "userJump":
			return "0";
		default:
			return "0";
		}
	}

	private static string GetMountParamChineseName(string key, bool isActionInfo)
	{
		string k = (key ?? "").Trim();
		if (string.IsNullOrEmpty(k))
			return "";
		switch (k.ToLowerInvariant())
		{
		case "tamingmob": return "数据ID绑定";
		case "icon": return "图标";
		case "iconraw": return "原始图标";
		case "islot": return "道具槽类型";
		case "vslot": return "可视槽类型";
		case "reqjob": return "需求职业";
		case "reqlevel": return "需求等级";
		case "tradeblock": return "禁止交易";
		case "notsale": return "不可出售";
		case "cash": return "现金标记";
		case "passengernum": return "乘客数量";
		case "removebody": return "隐藏身体";
		case "invisibleweapon": return "隐藏武器";
		case "invisiblecape": return "隐藏披风";
		case "incspeed": return "速度加成";
		case "incjump": return "跳跃加成";
		case "incfatigue": return "疲劳修正";
		case "speed": return "移动速度";
		case "jump": return "跳跃能力";
		case "fs": return "摩擦系数";
		case "swim": return "游泳能力";
		case "fatigue": return "疲劳增长";
		case "continentmove": return "跨图移动";
		case "userspeed": return "叠加角色速度";
		case "userjump": return "叠加角色跳跃";
		default:
			if (k.StartsWith("passengerNavel", StringComparison.OrdinalIgnoreCase))
				return "乘客锚点";
			if (k.StartsWith("vehicle", StringComparison.OrdinalIgnoreCase))
				return "载具能力";
			return isActionInfo ? "动作参数" : "数据参数";
		}
	}

	private List<string> ShowMountParamPicker(string title, IEnumerable<string> allKeys, HashSet<string> existing)
	{
		List<string> keyList = allKeys?.Where((string k) => !string.IsNullOrWhiteSpace(k))
			.Distinct(StringComparer.OrdinalIgnoreCase)
			.OrderBy((string k) => k, StringComparer.OrdinalIgnoreCase)
			.ToList() ?? new List<string>();
		if (keyList.Count == 0)
		{
			MessageBox.Show("没有可选参数。", "提示");
			return null;
		}

		Form form = new Form
		{
			Text = title,
			Size = new Size(430, 560),
			StartPosition = FormStartPosition.Manual,
			FormBorderStyle = FormBorderStyle.SizableToolWindow,
			MinimizeBox = false,
			MaximizeBox = false
		};
		try
		{
			int x = this.Left + Math.Max(20, (this.Width - form.Width) / 2);
			int y = this.Top + Math.Max(120, (this.Height - form.Height) / 2 + 80);
			form.Location = new Point(Math.Max(0, x), Math.Max(0, y));
		}
		catch
		{
		}

		Label label = new Label
		{
			Text = "勾选参数（已存在项不会重复添加）",
			Dock = DockStyle.Top,
			Height = 22,
			TextAlign = ContentAlignment.MiddleLeft
		};
		form.Controls.Add(label);

		CheckedListBox checkedListBox = new CheckedListBox
		{
			Dock = DockStyle.Fill,
			CheckOnClick = true
		};
		Dictionary<int, string> map = new Dictionary<int, string>();
		for (int i = 0; i < keyList.Count; i++)
		{
			string key = keyList[i];
			bool has = existing != null && existing.Contains(key);
			bool isActionInfo = title.Contains("动作");
			string display = key + "（" + GetMountParamChineseName(key, isActionInfo) + "）";
			int idx = checkedListBox.Items.Add(has ? (display + "  (已存在)") : display, false);
			map[idx] = key;
		}
		form.Controls.Add(checkedListBox);

		Panel panel = new Panel
		{
			Dock = DockStyle.Bottom,
			Height = 44
		};
		Button btnAll = new Button { Text = "全选", Location = new Point(8, 9), Width = 60 };
		btnAll.Click += delegate
		{
			for (int i = 0; i < checkedListBox.Items.Count; i++)
				checkedListBox.SetItemChecked(i, true);
		};
		panel.Controls.Add(btnAll);
		Button btnNone = new Button { Text = "全不选", Location = new Point(74, 9), Width = 66 };
		btnNone.Click += delegate
		{
			for (int i = 0; i < checkedListBox.Items.Count; i++)
				checkedListBox.SetItemChecked(i, false);
		};
		panel.Controls.Add(btnNone);
		Button btnOk = new Button { Text = "添加", Location = new Point(248, 9), Width = 72, DialogResult = DialogResult.OK };
		panel.Controls.Add(btnOk);
		Button btnCancel = new Button { Text = "取消", Location = new Point(326, 9), Width = 72, DialogResult = DialogResult.Cancel };
		panel.Controls.Add(btnCancel);
		form.Controls.Add(panel);
		form.AcceptButton = btnOk;
		form.CancelButton = btnCancel;

		if (form.ShowDialog(this) != DialogResult.OK)
			return null;

		List<string> result = new List<string>();
		foreach (int idx in checkedListBox.CheckedIndices)
		{
			if (map.TryGetValue(idx, out string key) && !string.IsNullOrWhiteSpace(key))
				result.Add(key);
		}
		return result;
	}

	private void ShowMountInfoToolTip(DataGridView grid, int rowIndex, int columnIndex, int mouseX, int mouseY)
	{
		if (_toolTip == null || grid == null)
			return;

		string text = BuildMountInfoCellHint(grid, rowIndex, columnIndex);
		if (string.IsNullOrWhiteSpace(text) || rowIndex < 0 || columnIndex < 0)
		{
			// Keep current tip until mouse leaves the grid to avoid flicker/disappear.
			return;
		}

		if (_lastMountTipGrid == grid
			&& _lastMountTipRow == rowIndex
			&& _lastMountTipCol == columnIndex
			&& string.Equals(_lastMountTipText, text, StringComparison.Ordinal))
		{
			return;
		}

		if (_lastMountTipGrid != null)
			_toolTip.Hide(_lastMountTipGrid);

		if (mouseX >= 0 && mouseY >= 0)
		{
			_toolTip.Show(text, grid, mouseX + 16, mouseY + 16, 20000);
		}
		else
		{
			Rectangle rect = grid.GetCellDisplayRectangle(columnIndex, rowIndex, cutOverflow: false);
			_toolTip.Show(text, grid, rect.Left + 12, rect.Bottom + 8, 20000);
		}

		_lastMountTipGrid = grid;
		_lastMountTipRow = rowIndex;
		_lastMountTipCol = columnIndex;
		_lastMountTipText = text;
	}

	private string BuildMountInfoCellHint(DataGridView grid, int rowIndex, int columnIndex)
	{
		if (grid == null || rowIndex < 0 || columnIndex < 0 || rowIndex >= grid.Rows.Count || columnIndex >= grid.Columns.Count)
			return null;

		string key = grid.Rows[rowIndex].Cells[0].Value?.ToString()?.Trim() ?? "";
		bool isActionInfo = ReferenceEquals(grid, dgvMountActionInfo);
		string help = GetMountParamHelp(key, isActionInfo);

		if (columnIndex == 0)
		{
			if (string.IsNullOrWhiteSpace(key))
				return isActionInfo
					? "参数名（动作IMG info）\n常用：tamingMob、reqLevel、removeBody、incSpeed..."
					: "参数名（Data IMG info）\n常用：speed、jump、fatigue、fs、swim...";
			return $"参数名：{key}\n作用：{help}";
		}

		if (string.IsNullOrWhiteSpace(key))
			return "参数值。可填写整数或字符串。";

		string value = grid.Rows[rowIndex].Cells[1].Value?.ToString()?.Trim() ?? "";
		return $"参数值：{key} = {value}\n作用：{help}";
	}

	private static string GetMountParamHelp(string key, bool isActionInfo)
	{
		string text = (key ?? "").Trim().ToLowerInvariant();
		if (string.IsNullOrEmpty(text))
			return isActionInfo ? "动作资源 info 参数。" : "坐骑移动数据参数。";

		switch (text)
		{
		case "tamingmob":
			return "动作文件绑定的 DataID，会指向 TamingMob/000x.img。";
		case "speed":
			return "地面移动速度。";
		case "jump":
			return "跳跃能力。";
		case "fatigue":
			return "疲劳增长值。";
		case "fs":
			return "摩擦/滑行手感参数。";
		case "swim":
			return "水中移动能力。";
		case "userspeed":
			return "是否叠加角色自身速度能力。";
		case "userjump":
			return "是否叠加角色自身跳跃能力。";
		case "continentmove":
			return "大陆移动/跨图移动开关。";
		case "reqjob":
			return "使用该坐骑资源所需职业。";
		case "reqlevel":
			return "使用该坐骑资源所需等级。";
		case "tradeblock":
			return "禁止交易标记。";
		case "notsale":
			return "不可出售标记。";
		case "cash":
			return "现金道具资源标记。";
		case "removebody":
			return "隐藏角色身体。";
		case "invisibleweapon":
			return "隐藏角色武器。";
		case "invisiblecape":
			return "隐藏角色披风。";
		case "passengernum":
			return "可乘坐人数。";
		case "incspeed":
			return "额外速度加成。";
		case "incjump":
			return "额外跳跃加成。";
		case "incfatigue":
			return "疲劳变化修正。";
		case "incstr":
		case "incdex":
		case "incint":
		case "incluk":
			return "属性加成字段。";
		case "incpad":
		case "incpdd":
		case "incmdd":
		case "incmhp":
		case "incmmp":
		case "inceva":
			return "战斗属性加成字段。";
		default:
			if (text.StartsWith("passengernavel"))
				return "乘客锚点位置（乘客贴图定位）。";
			if (text.StartsWith("vehicle"))
				return "坐骑车辆/飞行能力相关参数。";
			return isActionInfo ? "动作资源 info 参数（显示/限制/绑定相关）。" : "坐骑数据参数（移动/疲劳相关）。";
		}
	}

	private void BtnMountLoad_Click(object sender, EventArgs e)
	{
		if (!int.TryParse((txtMountEditorItemId?.Text ?? "").Trim(), out int mountItemId) || mountItemId <= 0)
		{
			MessageBox.Show("请输入有效的目标ItemId", "提示");
			return;
		}

		try
		{
			bool existsBeforeLoad = File.Exists(PathConfig.GameMountActionImg(mountItemId));
			LoadMountEditor(mountItemId);
			Console.WriteLine($"[坐骑编辑] 已加载 mountItemId={mountItemId}");
			if (!existsBeforeLoad)
				MessageBox.Show("该坐骑动作IMG不存在，已进入新建模式。\n保存时会自动创建并绑定 DataID。", "提示");
		}
		catch (Exception ex)
		{
			MessageBox.Show("加载坐骑失败: " + ex.Message, "错误");
		}
	}

	private void BtnMountClone_Click(object sender, EventArgs e)
	{
		if (!int.TryParse((txtMountEditorItemId?.Text ?? "").Trim(), out int targetMountItemId) || targetMountItemId <= 0)
		{
			MessageBox.Show("请输入有效的坐骑ItemId", "提示");
			return;
		}

		int sourceHint = 0;
		int.TryParse((txtMountEditorSourceItemId?.Text ?? "").Trim(), out sourceHint);
		if (sourceHint <= 0)
			int.TryParse((txtMountSourceItemId?.Text ?? "").Trim(), out sourceHint);
		if (sourceHint <= 0 && _editState != null && _editState.EditingListIndex.HasValue)
		{
			int idx = _editState.EditingListIndex.Value;
			if (idx >= 0 && idx < _pendingSkills.Count)
				sourceHint = ResolveMountSourceHint(_pendingSkills[idx]);
		}

		string sourceText = ShowInputBox("坐骑克隆", "来源坐骑ItemId:", sourceHint > 0 ? sourceHint.ToString() : "");
		if (sourceText == null)
			return;
		if (!int.TryParse((sourceText ?? "").Trim(), out int sourceMountItemId) || sourceMountItemId <= 0)
		{
			MessageBox.Show("来源坐骑ItemId无效", "提示");
			return;
		}

		try
		{
			int targetDataId = ResolveAutoTargetDataId(targetMountItemId, sourceMountItemId);
			MountEditorService.CloneMount(sourceMountItemId, targetMountItemId, targetDataId, cloneData: true);
			LoadMountEditor(targetMountItemId);
			TrackCustomMountId(targetMountItemId, refresh: true);
			if (txtMountEditorSourceItemId != null)
				txtMountEditorSourceItemId.Text = sourceMountItemId.ToString();
			Console.WriteLine($"[坐骑编辑] 已克隆 {sourceMountItemId} -> {targetMountItemId} (DataID={targetDataId})");
			MessageBox.Show($"克隆完成\n来源: {sourceMountItemId}\n目标: {targetMountItemId}\n绑定 DataID: {targetDataId}", "完成");
		}
		catch (Exception ex)
		{
			MessageBox.Show("克隆失败: " + ex.Message, "错误");
		}
	}

	private void BtnMountSaveAction_Click(object sender, EventArgs e)
	{
		try
		{
			if (!SyncMountEditorIntoModel(requireActionFrames: false))
				return;
			MountEditorService.SaveAction(_mountEditorData);
			TrackCustomMountId(_mountEditorData.MountItemId, refresh: true);
			Console.WriteLine($"[坐骑编辑] 动作IMG已保存: {PathConfig.GameMountActionImg(_mountEditorData.MountItemId)}");
		}
		catch (Exception ex)
		{
			MessageBox.Show("保存动作IMG失败: " + ex.Message, "错误");
		}
	}

	private void BtnMountSaveData_Click(object sender, EventArgs e)
	{
		try
		{
			if (!SyncMountEditorIntoModel(requireActionFrames: false))
				return;
			if (_mountEditorData.TamingMobId <= 0)
			{
				MessageBox.Show("DataID 无效，无法保存参数IMG", "提示");
				return;
			}
			MountEditorService.SaveData(_mountEditorData);
			Console.WriteLine($"[坐骑编辑] 参数IMG已保存: {PathConfig.GameMountDataImg(_mountEditorData.TamingMobId)}");
		}
		catch (Exception ex)
		{
			MessageBox.Show("保存参数IMG失败: " + ex.Message, "错误");
		}
	}

	private void BtnMountSyncXml_Click(object sender, EventArgs e)
	{
		try
		{
			if (!SyncMountEditorIntoModel(requireActionFrames: false))
				return;
			MountEditorService.SyncXml(_mountEditorData);
			Console.WriteLine("[坐骑编辑] 已同步服务端XML");
		}
		catch (Exception ex)
		{
			MessageBox.Show("同步XML失败: " + ex.Message, "错误");
		}
	}

	private void BtnMountSaveAll_Click(object sender, EventArgs e)
	{
		try
		{
			if (!SyncMountEditorIntoModel(requireActionFrames: false))
				return;

			MountEditorService.SaveAction(_mountEditorData);
			if (_mountEditorData.TamingMobId > 0)
				MountEditorService.SaveData(_mountEditorData);
			MountEditorService.SyncXml(_mountEditorData);
			TrackCustomMountId(_mountEditorData.MountItemId, refresh: true);
			Console.WriteLine("[坐骑编辑] 已全部保存（动作+参数+XML）");
			MessageBox.Show("坐骑资源保存完成", "完成");
		}
		catch (Exception ex)
		{
			MessageBox.Show("全部保存失败: " + ex.Message, "错误");
		}
	}

	private void BtnMountApplyToSkill_Click(object sender, EventArgs e)
	{
		if (txtMountEditorItemId == null || txtMountItemId == null)
			return;

		txtMountItemId.Text = txtMountEditorItemId.Text.Trim();
		txtMountSourceItemId.Text = txtMountEditorSourceItemId?.Text?.Trim() ?? "";
		txtMountTamingMobId.Text = (_mountEditorData != null && _mountEditorData.TamingMobId > 0)
			? _mountEditorData.TamingMobId.ToString()
			: (txtMountEditorDataId?.Text?.Trim() ?? "");

		if (cboMountResourceMode != null)
			cboMountResourceMode.SelectedIndex = Math.Min(cboMountResourceMode.Items.Count - 1, 2);

		// Advanced overrides are now managed by mount editor data file directly.
		if (txtMountSpeed != null) txtMountSpeed.Text = "";
		if (txtMountJump != null) txtMountJump.Text = "";
		if (txtMountFatigue != null) txtMountFatigue.Text = "";

		Console.WriteLine("[坐骑编辑] 已写回技能编辑页的 mountItemId/资源参数");
	}

	private void BtnMountAddNode_Click(object sender, EventArgs e)
	{
		if (_mountEditorData == null)
			_mountEditorData = new MountEditorData();

		string name = ShowInputBox("新增动作节点", "节点名（如 stand1/walk1/fly）:", "stand1");
		if (string.IsNullOrWhiteSpace(name))
			return;
		name = name.Trim();

		if (_mountEditorData.ActionFramesByNode.ContainsKey(name))
		{
			MessageBox.Show("节点已存在", "提示");
			return;
		}

		_mountEditorData.ActionFramesByNode[name] = new List<WzEffectFrame>();
		if (_mountEditorData.RemovedActionNodes != null)
			_mountEditorData.RemovedActionNodes.Remove(name);

		RefreshMountActionNodeSelector(name, createIfMissing: false);
		PopulateMountFrames(GetActiveMountFrames(createIfMissing: false));
	}

	private void BtnMountRemoveNode_Click(object sender, EventArgs e)
	{
		if (_mountEditorData == null || cboMountActionNode == null || cboMountActionNode.SelectedItem == null)
			return;

		string node = GetSelectedMountActionNodeKey();
		if (string.IsNullOrWhiteSpace(node))
			return;

		if (MessageBox.Show("删除动作节点 " + node + " ?", "确认", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation) != DialogResult.Yes)
			return;

		if (_mountEditorData.ActionFramesByNode != null)
			_mountEditorData.ActionFramesByNode.Remove(node);
		_mountEditorData.RemovedActionNodes.Add(node);

		RefreshMountActionNodeSelector(null, createIfMissing: false);
		PopulateMountFrames(GetActiveMountFrames(createIfMissing: false));
	}

	private static string GetMountActionNodeDisplayName(string nodeKey, int frameCount)
	{
		string key = (nodeKey ?? "").Trim();
		if (string.IsNullOrEmpty(key))
			return "";
		if (MountActionNodeLabels.TryGetValue(key, out string zh) && !string.IsNullOrWhiteSpace(zh))
			return key + "（" + zh + "） [" + Math.Max(frameCount, 0) + "帧]";
		return key + " [" + Math.Max(frameCount, 0) + "帧]";
	}

	private string GetSelectedMountActionNodeKey()
	{
		if (cboMountActionNode == null)
			return "";
		if (cboMountActionNode.SelectedItem is MountActionNodeItem item)
			return item.Key ?? "";
		return cboMountActionNode.SelectedItem?.ToString()?.Trim() ?? "";
	}

	private int ResolveAutoTargetDataId(int targetMountItemId, int sourceMountItemId)
	{
		if (MountEditorService.TryReadActionTamingMobIdByMountItem(targetMountItemId, out int existingDataId) && existingDataId > 0)
			return existingDataId;

		int sourceDataId = 0;
		MountEditorService.TryReadActionTamingMobIdByMountItem(sourceMountItemId, out sourceDataId);

		int preferred = sourceDataId > 0 ? sourceDataId : Math.Abs(targetMountItemId % 10000);
		int chosen = MountEditorService.FindNextAvailableTamingMobId(preferred);
		if (chosen <= 0)
			chosen = MountEditorService.FindNextAvailableTamingMobId(1);
		return chosen;
	}

	private void LoadMountEditor(int mountItemId)
	{
		bool actionExists = File.Exists(PathConfig.GameMountActionImg(mountItemId));
		try { _mountEditorData?.Dispose(); } catch { }
		_mountEditorData = MountEditorService.Load(mountItemId);

		txtMountEditorItemId.Text = mountItemId.ToString();
		if (_mountEditorData.TamingMobId <= 0)
		{
			int autoDataId = MountEditorService.FindNextAvailableTamingMobId(Math.Abs(mountItemId % 10000));
			_mountEditorData.TamingMobId = autoDataId;
			if (_mountEditorData.ActionInfo == null)
				_mountEditorData.ActionInfo = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
			_mountEditorData.ActionInfo["tamingMob"] = autoDataId.ToString();
		}

		txtMountEditorDataId.Text = (_mountEditorData.TamingMobId > 0) ? _mountEditorData.TamingMobId.ToString() : "";
		if (lblMountEditorDataBinding != null)
			lblMountEditorDataBinding.Text = (_mountEditorData.TamingMobId > 0)
				? ("DataID 自动绑定: " + _mountEditorData.TamingMobId)
				: "DataID: 自动读取";

		if (_mountEditorData.ActionInfo == null)
			_mountEditorData.ActionInfo = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
		if (_mountEditorData.DataInfo == null)
			_mountEditorData.DataInfo = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
		if (_mountEditorData.ActionFramesByNode == null)
			_mountEditorData.ActionFramesByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
		if (_mountEditorData.TamingMobId > 0
			&& _mountEditorData.DataInfo.Count == 0
			&& !File.Exists(PathConfig.GameMountDataImg(_mountEditorData.TamingMobId)))
		{
			_mountEditorData.DataInfo["speed"] = "140";
			_mountEditorData.DataInfo["jump"] = "120";
			_mountEditorData.DataInfo["fatigue"] = "1";
			_mountEditorData.DataInfo["fs"] = "0";
			_mountEditorData.DataInfo["swim"] = "0";
		}
		if (!actionExists && _mountEditorData.ActionFramesByNode.Count == 0)
			_mountEditorData.ActionFramesByNode["stand1"] = new List<WzEffectFrame>();

		PopulateMountInfoGrid(dgvMountActionInfo, _mountEditorData.ActionInfo);
		PopulateMountInfoGrid(dgvMountDataInfo, _mountEditorData.DataInfo);
		RefreshMountActionNodeSelector("stand1", createIfMissing: false);
		PopulateMountFrames(GetActiveMountFrames(createIfMissing: false));

		if (!actionExists)
			Console.WriteLine($"[坐骑编辑] 目标动作IMG不存在，当前为新建模式: {PathConfig.GameMountActionImg(mountItemId)}");
		else
		{
			int totalFrames = 0;
			foreach (var kv in _mountEditorData.ActionFramesByNode)
				totalFrames += kv.Value?.Count ?? 0;
			if (totalFrames <= 0)
			{
				Console.WriteLine($"[坐骑编辑] 已加载 {mountItemId}，但未解析到帧数据。请检查该文件是否为特殊结构/UOL格式。");
			}
		}
	}

	private bool SyncMountEditorIntoModel(bool requireActionFrames)
	{
		if (!int.TryParse((txtMountEditorItemId?.Text ?? "").Trim(), out int mountItemId) || mountItemId <= 0)
		{
			MessageBox.Show("目标ItemId无效", "提示");
			return false;
		}

		if (_mountEditorData == null)
			_mountEditorData = new MountEditorData();

		_mountEditorData.MountItemId = mountItemId;
		_mountEditorData.ActionImgPath = PathConfig.GameMountActionImg(mountItemId);

		_mountEditorData.ActionInfo = ReadMountInfoGrid(dgvMountActionInfo);
		_mountEditorData.DataInfo = ReadMountInfoGrid(dgvMountDataInfo);
		int dataId = 0;
		if (_mountEditorData.ActionInfo != null
			&& _mountEditorData.ActionInfo.TryGetValue("tamingMob", out string autoDataText)
			&& int.TryParse((autoDataText ?? "").Trim(), out int parsedFromInfo)
			&& parsedFromInfo > 0)
		{
			dataId = parsedFromInfo;
		}
		else if (!int.TryParse((txtMountEditorDataId?.Text ?? "").Trim(), out dataId) || dataId <= 0)
		{
			dataId = _mountEditorData.TamingMobId;
		}
		_mountEditorData.TamingMobId = dataId;
		_mountEditorData.DataImgPath = dataId > 0 ? PathConfig.GameMountDataImg(dataId) : "";
		if (_mountEditorData.TamingMobId > 0)
			_mountEditorData.ActionInfo["tamingMob"] = _mountEditorData.TamingMobId.ToString();
		if (txtMountEditorDataId != null)
			txtMountEditorDataId.Text = (_mountEditorData.TamingMobId > 0) ? _mountEditorData.TamingMobId.ToString() : "";
		if (lblMountEditorDataBinding != null)
			lblMountEditorDataBinding.Text = (_mountEditorData.TamingMobId > 0)
				? ("DataID 自动绑定: " + _mountEditorData.TamingMobId)
				: "DataID: 自动读取";

		if (requireActionFrames)
		{
			bool hasFrames = false;
			if (_mountEditorData.ActionFramesByNode != null)
			{
				foreach (var kv in _mountEditorData.ActionFramesByNode)
				{
					if (kv.Value != null && kv.Value.Count > 0)
					{
						hasFrames = true;
						break;
					}
				}
			}
			if (!hasFrames)
			{
				MessageBox.Show("动作节点为空，请先添加至少一帧", "提示");
				return false;
			}
		}
		return true;
	}

	private void PopulateMountInfoGrid(DataGridView grid, Dictionary<string, string> map)
	{
		if (grid == null)
			return;
		grid.Rows.Clear();
		if (map == null || map.Count == 0)
			return;

		var keys = new List<string>(map.Keys);
		keys.Sort(StringComparer.OrdinalIgnoreCase);
		foreach (string key in keys)
		{
			if (string.IsNullOrWhiteSpace(key))
				continue;
			map.TryGetValue(key, out string value);
			grid.Rows.Add(key, value ?? "");
		}
	}

	private Dictionary<string, string> ReadMountInfoGrid(DataGridView grid)
	{
		var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
		if (grid == null)
			return map;

		foreach (DataGridViewRow row in grid.Rows)
		{
			if (row == null || row.IsNewRow)
				continue;
			string key = row.Cells[0].Value?.ToString()?.Trim();
			if (string.IsNullOrWhiteSpace(key))
				continue;
			string value = row.Cells[1].Value?.ToString()?.Trim() ?? "";
			map[key] = value;
		}
		return map;
	}

	private string FindGridValue(DataGridView grid, string key)
	{
		if (grid == null || string.IsNullOrWhiteSpace(key))
			return "";
		foreach (DataGridViewRow row in grid.Rows)
		{
			if (row == null || row.IsNewRow)
				continue;
			string rowKey = row.Cells[0].Value?.ToString()?.Trim();
			if (string.Equals(rowKey, key, StringComparison.OrdinalIgnoreCase))
				return row.Cells[1].Value?.ToString()?.Trim() ?? "";
		}
		return "";
	}

	private List<WzEffectFrame> GetActiveMountFrames(bool createIfMissing)
	{
		if (_mountEditorData == null || cboMountActionNode == null)
			return null;

		string node = GetSelectedMountActionNodeKey();
		if (string.IsNullOrWhiteSpace(node))
			return null;
		node = node.Trim();

		if (_mountEditorData.ActionFramesByNode == null)
			_mountEditorData.ActionFramesByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);

		if (!_mountEditorData.ActionFramesByNode.TryGetValue(node, out var list) && createIfMissing)
		{
			list = new List<WzEffectFrame>();
			_mountEditorData.ActionFramesByNode[node] = list;
		}
		return list;
	}

	private void RefreshMountActionNodeSelector(string preferredNode, bool createIfMissing)
	{
		if (cboMountActionNode == null || _mountEditorData == null)
			return;

		if (_mountEditorData.ActionFramesByNode == null)
			_mountEditorData.ActionFramesByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);

		if (_mountEditorData.ActionFramesByNode.Count == 0 && createIfMissing)
			_mountEditorData.ActionFramesByNode["stand1"] = new List<WzEffectFrame>();

		var nodes = new List<string>();
		foreach (string key in MountActionDefaultNodeKeys)
		{
			if (!nodes.Exists((string n) => string.Equals(n, key, StringComparison.OrdinalIgnoreCase)))
				nodes.Add(key);
		}
		var extraNodes = new List<string>(_mountEditorData.ActionFramesByNode.Keys);
		extraNodes.Sort(StringComparer.OrdinalIgnoreCase);
		foreach (string key in extraNodes)
		{
			if (!nodes.Exists((string n) => string.Equals(n, key, StringComparison.OrdinalIgnoreCase)))
				nodes.Add(key);
		}

		string selected = preferredNode;
		if (string.IsNullOrWhiteSpace(selected))
			selected = GetSelectedMountActionNodeKey();
		if (string.IsNullOrWhiteSpace(selected))
			selected = nodes.Count > 0 ? nodes[0] : "";
		if (nodes.Count > 0 && !nodes.Exists((string n) => string.Equals(n, selected, StringComparison.OrdinalIgnoreCase)))
			selected = nodes[0];

		_suppressMountNodeChange = true;
		try
		{
			cboMountActionNode.BeginUpdate();
			cboMountActionNode.Items.Clear();
			foreach (string node in nodes)
			{
				int frameCount = 0;
				if (_mountEditorData.ActionFramesByNode != null
					&& _mountEditorData.ActionFramesByNode.TryGetValue(node, out List<WzEffectFrame> frames)
					&& frames != null)
				{
					frameCount = frames.Count;
				}
				cboMountActionNode.Items.Add(new MountActionNodeItem
				{
					Key = node,
					Display = GetMountActionNodeDisplayName(node, frameCount)
				});
			}
			if (nodes.Count > 0)
			{
				foreach (object obj in cboMountActionNode.Items)
				{
					if (obj is MountActionNodeItem item
						&& string.Equals(item.Key, selected, StringComparison.OrdinalIgnoreCase))
					{
						cboMountActionNode.SelectedItem = obj;
						break;
					}
				}
				if (cboMountActionNode.SelectedIndex < 0)
					cboMountActionNode.SelectedIndex = 0;
			}
			else
				cboMountActionNode.Text = "";
		}
		finally
		{
			cboMountActionNode.EndUpdate();
			_suppressMountNodeChange = false;
		}
	}

	private void CboMountActionNode_SelectedIndexChanged(object sender, EventArgs e)
	{
		if (_suppressMountNodeChange)
			return;
		PopulateMountFrames(GetActiveMountFrames(createIfMissing: false));
	}

	private void PopulateMountFrames(List<WzEffectFrame> frames)
	{
		if (lvMountFrames == null)
			return;
		lvMountFrames.Items.Clear();
		if (frames == null || frames.Count == 0)
		{
			if (picMountFramePreview != null)
				picMountFramePreview.Image = null;
			return;
		}

		for (int i = 0; i < frames.Count; i++)
		{
			WzEffectFrame frame = frames[i];
			if (frame == null) continue;
			frame.Index = i;
			var item = new ListViewItem(frame.Index.ToString());
			item.SubItems.Add($"{frame.Width}x{frame.Height}");
			item.SubItems.Add(frame.Delay.ToString());
			item.SubItems.Add(BuildEffectFrameMetaSummary(frame));
			lvMountFrames.Items.Add(item);
		}
	}

	private void LvMountFrames_SelectedChanged(object sender, EventArgs e)
	{
		List<WzEffectFrame> frames = GetActiveMountFrames(createIfMissing: false);
		if (lvMountFrames == null || picMountFramePreview == null || lvMountFrames.SelectedIndices.Count == 0 || frames == null)
		{
			if (picMountFramePreview != null) picMountFramePreview.Image = null;
			return;
		}
		int idx = lvMountFrames.SelectedIndices[0];
		if (idx >= 0 && idx < frames.Count)
			SafeSetImage(picMountFramePreview, frames[idx].Bitmap);
		else
			picMountFramePreview.Image = null;
	}

	private bool TryGetSelectedMountFrame(out WzEffectFrame frame, out int index)
	{
		frame = null;
		index = -1;
		List<WzEffectFrame> frames = GetActiveMountFrames(createIfMissing: false);
		if (lvMountFrames == null || lvMountFrames.SelectedIndices.Count == 0 || frames == null)
			return false;
		index = lvMountFrames.SelectedIndices[0];
		if (index < 0 || index >= frames.Count)
			return false;
		frame = frames[index];
		return frame != null;
	}

	private void MountMenu_AddFrame(object sender, EventArgs e)
	{
		List<WzEffectFrame> frames = GetActiveMountFrames(createIfMissing: true);
		if (frames == null) return;

		using (var ofd = new OpenFileDialog { Filter = "图片|*.png;*.bmp;*.jpg;*.jpeg;*.gif|全部文件|*.*" })
		{
			if (ofd.ShowDialog() != DialogResult.OK) return;
			try
			{
				Bitmap bmp = new Bitmap(ofd.FileName);
				int delay = 100;
				int.TryParse(ShowInputBox("添加帧", "延迟(ms):", "100"), out delay);
				frames.Add(new WzEffectFrame
				{
					Index = frames.Count,
					Bitmap = bmp,
					Width = bmp.Width,
					Height = bmp.Height,
					Delay = delay > 0 ? delay : 100
				});
				PopulateMountFrames(frames);
			}
			catch (Exception ex)
			{
				MessageBox.Show("添加帧失败: " + ex.Message, "错误");
			}
		}
	}

	private void MountMenu_ReplaceFrame(object sender, EventArgs e)
	{
		List<WzEffectFrame> frames = GetActiveMountFrames(createIfMissing: false);
		if (frames == null || lvMountFrames == null || lvMountFrames.SelectedIndices.Count == 0)
			return;
		int idx = lvMountFrames.SelectedIndices[0];
		if (idx < 0 || idx >= frames.Count) return;

		using (var ofd = new OpenFileDialog { Filter = "图片|*.png;*.bmp;*.jpg;*.jpeg;*.gif|全部文件|*.*" })
		{
			if (ofd.ShowDialog() != DialogResult.OK) return;
			try
			{
				Bitmap bmp = new Bitmap(ofd.FileName);
				frames[idx].Bitmap = bmp;
				frames[idx].Width = bmp.Width;
				frames[idx].Height = bmp.Height;
				PopulateMountFrames(frames);
			}
			catch (Exception ex)
			{
				MessageBox.Show("替换帧失败: " + ex.Message, "错误");
			}
		}
	}

	private void MountMenu_EditDelay(object sender, EventArgs e)
	{
		if (!TryGetSelectedMountFrame(out var frame, out _))
			return;
		string text = ShowInputBox("编辑延迟", "延迟(ms):", frame.Delay.ToString());
		if (text == null) return;
		if (int.TryParse(text, out int delay))
			frame.Delay = delay > 0 ? delay : frame.Delay;
		PopulateMountFrames(GetActiveMountFrames(createIfMissing: false));
	}

	private void MountMenu_EditFrameMeta(object sender, EventArgs e)
	{
		if (!TryGetSelectedMountFrame(out var frame, out int idx))
			return;

		string initialJson = BuildEffectFrameMetaJson(frame);
		string edited = ShowMultiLineInputBox("编辑帧定位/参数", "JSON格式：vectors里填x/y，props里填z等标量", initialJson);
		if (edited == null)
			return;

		if (!TryParseEffectFrameMetaJson(edited, out var vectors, out var frameProps, out var err))
		{
			MessageBox.Show("格式错误: " + err, "编辑失败");
			return;
		}

		frame.Vectors = vectors;
		frame.FrameProps = frameProps;
		PopulateMountFrames(GetActiveMountFrames(createIfMissing: false));
		if (lvMountFrames != null && idx >= 0 && idx < lvMountFrames.Items.Count)
		{
			lvMountFrames.Items[idx].Selected = true;
			lvMountFrames.Items[idx].Focused = true;
		}
	}

	private void MountMenu_CopyFrames(object sender, EventArgs e)
	{
		List<WzEffectFrame> frames = GetActiveMountFrames(createIfMissing: false);
		if (frames == null || lvMountFrames == null || lvMountFrames.SelectedIndices.Count == 0)
			return;

		_clipboardFrames = new List<WzEffectFrame>();
		foreach (int idx in lvMountFrames.SelectedIndices)
		{
			if (idx >= 0 && idx < frames.Count)
			{
				var cloned = WzEffectFrame.CloneShallowBitmap(frames[idx]);
				if (cloned != null)
					_clipboardFrames.Add(cloned);
			}
		}
		Console.WriteLine($"[坐骑编辑] 已复制 {_clipboardFrames.Count} 帧");
	}

	private void MountMenu_PasteFrames(object sender, EventArgs e)
	{
		if (_clipboardFrames == null || _clipboardFrames.Count == 0)
			return;
		List<WzEffectFrame> frames = GetActiveMountFrames(createIfMissing: true);
		if (frames == null) return;

		foreach (WzEffectFrame src in _clipboardFrames)
		{
			WzEffectFrame cloned = WzEffectFrame.CloneShallowBitmap(src);
			if (cloned != null)
			{
				cloned.Index = frames.Count;
				frames.Add(cloned);
			}
		}
		PopulateMountFrames(frames);
	}

	private void MountMenu_DeleteFrame(object sender, EventArgs e)
	{
		List<WzEffectFrame> frames = GetActiveMountFrames(createIfMissing: false);
		if (frames == null || lvMountFrames == null || lvMountFrames.SelectedIndices.Count == 0)
			return;

		List<int> indexes = new List<int>();
		foreach (int idx in lvMountFrames.SelectedIndices)
		{
			if (idx >= 0 && idx < frames.Count)
				indexes.Add(idx);
		}
		if (indexes.Count == 0)
			return;
		if (MessageBox.Show($"确认删除选中的 {indexes.Count} 帧吗？", "确认删除", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation) != DialogResult.Yes)
			return;

		indexes.Sort();
		for (int i = indexes.Count - 1; i >= 0; i--)
			frames.RemoveAt(indexes[i]);

		ReindexEffectFrames(frames);
		PopulateMountFrames(frames);
		Console.WriteLine($"[坐骑编辑] 已删除 {indexes.Count} 帧");

		int next = Math.Min(indexes[0], frames.Count - 1);
		if (next >= 0 && next < lvMountFrames.Items.Count)
		{
			lvMountFrames.Items[next].Selected = true;
			lvMountFrames.Items[next].Focused = true;
		}
	}

	private void MountFrames_DragEnter(object sender, DragEventArgs e)
	{
		if (e.Data != null && e.Data.GetDataPresent(DataFormats.FileDrop))
			e.Effect = DragDropEffects.Copy;
	}

	private void MountFrames_DragDrop(object sender, DragEventArgs e)
	{
		if (e.Data == null || !e.Data.GetDataPresent(DataFormats.FileDrop))
			return;

		string[] files = e.Data.GetData(DataFormats.FileDrop) as string[];
		if (files == null || files.Length == 0)
			return;

		List<WzEffectFrame> frames = GetActiveMountFrames(createIfMissing: true);
		if (frames == null)
			return;

		int imported = 0;
		foreach (string file in files)
		{
			string ext = Path.GetExtension(file)?.ToLowerInvariant();
			if (ext != ".png" && ext != ".bmp" && ext != ".jpg" && ext != ".jpeg" && ext != ".gif")
				continue;
			try
			{
				Bitmap bmp = new Bitmap(file);
				frames.Add(new WzEffectFrame
				{
					Index = frames.Count,
					Bitmap = bmp,
					Width = bmp.Width,
					Height = bmp.Height,
					Delay = 100
				});
				imported++;
			}
			catch { }
		}
		if (imported > 0)
		{
			PopulateMountFrames(frames);
			Console.WriteLine($"[坐骑编辑] 拖拽导入 {imported} 帧");
		}
	}

	private void TrySyncMountEditorFromSkill(SkillDefinition sd)
	{
		if (sd == null || sd.MountItemId <= 0 || txtMountEditorItemId == null)
			return;

		txtMountEditorItemId.Text = sd.MountItemId.ToString();
		int sourceHint = (sd.MountSourceItemId > 0) ? sd.MountSourceItemId : ResolveMountSourceHint(sd);
		txtMountEditorSourceItemId.Text = sourceHint > 0 ? sourceHint.ToString() : "";
		txtMountEditorDataId.Text = (sd.MountTamingMobId > 0) ? sd.MountTamingMobId.ToString() : "";
		try
		{
			LoadMountEditor(sd.MountItemId);
		}
		catch (Exception ex)
		{
			Console.WriteLine("[坐骑编辑] 自动同步加载失败: " + ex.Message);
		}
	}

	private TextBox AddSettingRow(Panel parent, ref int y, string label, string value, bool browse)
	{
		parent.Controls.Add(new Label
		{
			Text = label,
			Location = new Point(10, y + 3),
			AutoSize = true
		});
		TextBox tb = new TextBox
		{
			Location = new Point(160, y),
			Width = 540,
			Text = value
		};
		parent.Controls.Add(tb);
		if (browse)
		{
			Button button = new Button
			{
				Text = "...",
				Location = new Point(710, y - 1),
				Width = 32
			};
			button.Click += delegate
			{
				FolderBrowserDialog folderBrowserDialog = new FolderBrowserDialog
				{
					SelectedPath = tb.Text
				};
				try
				{
					if (folderBrowserDialog.ShowDialog() == DialogResult.OK)
					{
						tb.Text = folderBrowserDialog.SelectedPath;
					}
				}
				finally
				{
					((IDisposable)(object)folderBrowserDialog)?.Dispose();
				}
			};
			parent.Controls.Add(button);
		}
		y += 30;
		return tb;
	}

	private void ConfigureToolTips()
	{
		if (_toolTip == null)
		{
			return;
		}
		SetControlTip(cboJobId, "选择职业后，会自动联动技能ID前缀。");
		SetControlTip(txtSkillNum, "输入职业内编号，系统会自动拼接成技能ID。");
		SetControlTip(txtSkillIdInput, "输入要读取的技能ID，按 Enter 或点“加载”读取 .img 数据。");
		SetControlTip(btnLoad, "从游戏 .img 读取技能数据（图标、特效、参数、节点树）。");
		SetControlTip(cboPreset, "常用模板技能快捷入口。");
		SetControlTip(btnLoadPreset, "按预设技能ID快速加载一个可修改样例。");
		SetControlTip(txtSkillLibrarySearch, "输入关键词搜索技能库（名称/描述/技能ID）。技能库来自 Data/Skill/*.img 全量扫描。");
		SetControlTip(chkSkillFilterName, "按技能名称字段参与搜索（可与 ID/描述多选组合）。");
		SetControlTip(chkSkillFilterId, "按技能ID字段参与搜索（可与 名字/描述多选组合）。");
		SetControlTip(chkSkillFilterDesc, "按技能描述字段参与搜索（可与 名字/ID多选组合）。");
		SetControlTip(lvSkillLibrary, "左侧技能库（含原版技能与新增超级技能）。双击或回车可直接载入该技能ID。");
		SetControlTip(lblSkillLibraryStatus, "显示当前筛选条数 / 技能库总条数。");

		SetControlTip(picIcon, "主图标。支持点击或拖拽替换。");
		SetControlTip(picIconMO, "鼠标悬停图标。支持点击或拖拽替换。");
		SetControlTip(picIconDis, "禁用图标。支持点击或拖拽替换。");

		SetControlTip(txtSkillId, "当前编辑技能ID。执行时按此ID写入。");
		SetControlTip(txtName, "技能名称。会写入 String/Skill。");
		SetControlTip(cboTab, "active=主动技能，passive=被动技能。");
		SetControlTip(nudMaxLevel, "技能最大等级。会同步写入 common/maxLevel。");
		SetControlTip(nudSuperSpCost, "每次升级消耗的超级SP。");
		SetControlTip(txtDesc, "技能描述。会写入 String/Skill。");

		SetControlTip(cboPacketRoute,
			"释放路由/发包类型。\n" +
			"close_range：近战攻击包（0x46）\n" +
			"ranged_attack：远程攻击包（0x47）\n" +
			"magic_attack：魔法攻击包（0x48）\n" +
			"special_move：主动BUFF/飞行/骑宠/变身（0x93）\n" +
			"skill_effect：技能表现/特效（0x95）\n" +
			"cancel_buff：取消BUFF（0x94）\n" +
			"special_attack：特殊攻击包（0xAA）");
		SetControlTip(txtProxySkillId, "代理技能ID（发包/逻辑复用）。");
		SetControlTip(txtVisualSkillId, "外观技能ID（只借用特效与展示）。");
		SetControlTip(cboReleaseClass, "释放分类实现类。通常保持默认。");
		SetControlTip(chkBorrowDonorVisual, "优先借用代理技能的可视表现。");
		SetControlTip(txtMountItemId, "坐骑ItemId（mountItemId）。其余 tamingMob/Data 参数由坐骑资源自动读取并绑定。");
		SetControlTip(chkAllowMountedFlight, "是否写出 allowMountedFlight=true。勾选后仅在“上坐骑+跳”且满足飞行技能条件时进入飞行。");
		SetControlTip(cboMountResourceMode,
			"坐骑资源同步模式：\n" +
			"仅写配置：只写 mountItemId，不改资源文件；\n" +
			"缺失时同步动作：缺失时创建 Character/TamingMob/0190xxxx.img；\n" +
			"同步动作+参数：在上一步基础上同步 TamingMob/000x.img。");
		SetControlTip(txtMountEditorItemId, "目标坐骑 ItemId。点击“加载/编辑”后自动读取 info/tamingMob 并绑定 Data 文件。");
		SetControlTip(lblMountEditorDataBinding, "当前绑定的 DataID（来自动作文件 info/tamingMob，或自动分配的新ID）。");
		SetControlTip(cboMountActionNode, "坐骑动作节点（stand1/walk1/jump/fly...）。");
		SetControlTip(lvMountFrames, "坐骑帧列表。支持右键编辑延迟、锚点、标量参数，也支持拖拽图片追加帧。");
		SetControlTip(picMountFramePreview, "当前选中坐骑帧预览。");
		SetControlTip(btnMountLoad, "加载当前目标坐骑资源到编辑器。");
		SetControlTip(btnMountClone, "输入来源坐骑ID，自动克隆动作与参数，并自动绑定可用 DataID。");
		SetControlTip(btnMountSaveAction, "保存 Character/TamingMob/0190xxxx.img。");
		SetControlTip(btnMountSaveData, "保存 TamingMob/000x.img。");
		SetControlTip(btnMountSyncXml, "把当前坐骑 .img 同步为服务端 XML。");
		SetControlTip(btnMountSaveAll, "保存动作+参数并同步服务端 XML。");
		SetControlTip(btnMountApplyToSkill, "将坐骑编辑器的 ItemId/DataID/参数回填到技能编辑页。");
		SetControlTip(btnMountAddNode, "新增动作节点（例如 stand1/walk1/fly）。");
		SetControlTip(btnMountRemoveNode, "删除当前动作节点（保存后生效）。");
		SetControlTip(cboMountKnownIds, "已添加/已配置的坐骑ID列表（来自待执行列表 + super_skills_server.json）。");
		SetControlTip(btnMountKnownLoad, "将下拉中选中的坐骑ID载入到编辑器。");
		SetControlTip(btnMountKnownRefresh, "重新读取坐骑ID列表。");

		SetControlTip(chkHideFromNative, "在原生技能窗隐藏该技能。");
		SetControlTip(chkShowInNativeWhenLearned, "学习后在原生技能窗显示。");
		SetControlTip(chkShowInSuperWhenLearned, "学习后在超级技能窗显示。");
		SetControlTip(chkAllowNativeFallback, "允许逻辑回退到原生技能处理。");
		SetControlTip(chkInjectToNative, "将技能注入原生技能流程（按需启用）。");

		SetControlTip(btnCopyEffects, "复制其它技能的 effect 帧数据到当前技能。");
		SetControlTip(cboEffectNode, "特效节点切换（effect/effect0...、repeat/repeat0...）。当前帧编辑操作只作用于选中节点。");
		SetControlTip(lvEffectFrames, "技能特效帧列表。支持右键编辑帧延迟/坐标参数，也支持拖拽图片追加帧。");
		SetControlTip(_picEffectPreview, "当前选中特效帧预览。");
		SetControlTip(_lblParamType, "参数编辑区模式提示：公共参数模式或按等级参数模式。");
		SetControlTip(dgvLevelParams, "参数编辑表。公共参数写入 common；等级参数写入 level。鼠标悬停单元格可看详细说明。");

		SetControlTip(btnAddToList, "把当前编辑内容加入待执行列表。");
		SetControlTip(btnUpdateSelected, "把当前编辑内容覆盖到列表中已选技能。");
		SetControlTip(btnRemoveFromList, "从列表移除并加入删除队列。");
		SetControlTip(btnClearList, "清空列表并把所有项加入删除队列。");
		SetControlTip(btnUndo, "回退上一步（支持添加/更新/删除/参数编辑等）。");
		SetControlTip(btnRedo, "恢复上一步回退的操作。");
		SetControlTip(lvSkills, "待执行技能列表。双击可编辑；右键支持删除或从 .img 重新加载。");
		SetControlTip(btnExecuteAdd, "正式写入所有目标（JSON/XML/.img/配置）。");
		SetControlTip(chkSkipImg, "勾选后跳过服务端 XML 相关写入。");

		SetControlTip(txtVerifyId, "输入技能ID，验证该技能在各目标文件中的一致性。");
		SetControlTip(txtRemoveId, "输入技能ID，执行跨文件删除。");
		SetControlTip(chkRemoveDryRun, "删除前演练，不实际修改文件。");

		SetControlTip(txtServerRootDir, "服务端根目录。其余服务端路径会自动派生。");
		SetControlTip(txtOutputDir, "SQL、清单、备份等输出目录。");
		SetControlTip(txtGameDataBaseDir, "游戏 Data 根目录。其余 Skill/Character/TamingMob 路径会自动派生。");
		SetControlTip(txtConfigDataDir, "配置数据保存目录（pending_skills.json、custom_mount_ids.json 等）。默认为当前运行目录。");
		SetControlTip(txtDefaultCarrierId, "默认超级SP载体技能ID。");
		SetControlTip(txtLog, "运行日志输出区。失败原因会显示在这里。");

		dgvLevelParams.ShowCellToolTips = false;

		ApplyTextBasedToolTips(this);
	}

	private void SetControlTip(Control control, string tip)
	{
		if (_toolTip == null || control == null || string.IsNullOrWhiteSpace(tip))
		{
			return;
		}
		if (ReferenceEquals(control, dgvMountActionInfo) || ReferenceEquals(control, dgvMountDataInfo))
		{
			_toolTip.SetToolTip(control, string.Empty);
			return;
		}
		_toolTip.SetToolTip(control, tip);
	}

	private void ApplyTextBasedToolTips(Control parent)
	{
		if (parent == null || _toolTip == null)
		{
			return;
		}
		foreach (Control child in parent.Controls)
		{
			if (!string.IsNullOrWhiteSpace(child.Text) && string.IsNullOrWhiteSpace(_toolTip.GetToolTip(child)))
			{
				string tip = GetTextBasedToolTip(child.Text.Trim());
				if (!string.IsNullOrWhiteSpace(tip))
				{
					_toolTip.SetToolTip(child, tip);
				}
			}
			if (child.HasChildren)
			{
				ApplyTextBasedToolTips(child);
			}
		}
	}

	private static string GetTextBasedToolTip(string text)
	{
		switch (text)
		{
		case "加载外观":
			return "按外观技能ID读取图标/特效并覆盖当前编辑区。";
		case "验证单个":
			return "验证单个技能在 JSON/XML/.img/配置中的数据是否齐全。";
		case "验证全部":
			return "对当前配置中的所有技能执行一致性验证。";
		case "删除技能":
			return "把该技能从所有目标文件中删除。";
		case "从JSON导入...":
			return "从外部 JSON 批量导入技能到当前列表。";
		case "导出当前列表...":
			return "将当前待执行列表导出为 JSON。";
		case "保存设置":
			return "保存路径与默认参数到配置文件。";
		case "恢复默认":
			return "把设置恢复到默认值。";
		case "...":
			return "选择目录。";
		default:
			return null;
		}
	}

	private void DgvLevelParams_CellToolTipTextNeeded(object sender, DataGridViewCellToolTipTextNeededEventArgs e)
	{
		if (e.RowIndex < 0 || e.ColumnIndex < 0 || dgvLevelParams == null)
		{
			return;
		}
		e.ToolTipText = BuildParamCellHint(e.RowIndex, e.ColumnIndex) ?? "";
	}

	private static string GetCommonParamHelp(string key)
	{
		string text = (key ?? "").Trim().ToLowerInvariant();
		switch (text)
		{
		case "maxlevel":
			return "技能最大等级（整数）。会写入 common/maxLevel，并与上方“最大等级”同步；它直接决定可升级上限。";
		case "pad":
			return "物理攻击力（PAD）固定值加成（非百分比）。示例：5+x。";
		case "mad":
			return "魔法攻击力（MAD）固定值加成（非百分比）。示例：5+x。";
		case "pdd":
			return "物理防御力（PDD）固定值加成（非百分比）。示例：10*x。常用于 Buff 技能（通常配合 time）。";
		case "mdd":
			return "魔法防御力（MDD）固定值加成（非百分比）。示例：5*x。常用于 Buff 技能（通常配合 time）。";
		case "acc":
			return "命中值（ACC）固定值加成（非百分比）。";
		case "eva":
			return "回避值（EVA）固定值加成（非百分比）。";
		case "speed":
			return "移动速度固定值加成。";
		case "jump":
			return "跳跃力固定值加成。";
		case "hp":
			return "HP数值参数，通常用于即时回复量（固定值）。若要“最大HP百分比加成”通常用 mhpR。";
		case "mp":
			return "MP数值参数，通常用于即时回复量（固定值）。若要“最大MP百分比加成”通常用 mmpR。";
		case "hpcon":
			return "释放技能时消耗的 HP（固定值/公式）。";
		case "mpcon":
			return "释放技能时消耗的 MP（固定值/公式）。常见写法：6+2*u(x/5) 或 6+2*d(x/5)，其中 x=当前技能等级（随等级同步变化），u(expr)=向上取整，d(expr)=向下取整。";
		case "padr":
			return "物理攻击力百分比加成（PAD%）。";
		case "madr":
			return "魔法攻击力百分比加成（MAD%）。";
		case "hpr":
			return "HP百分比回复（%）。常见写法：10 表示回复 10% HP。";
		case "mpr":
			return "MP百分比回复（%）。常见写法：10 表示回复 10% MP。";
		case "mhpr":
			return "最大HP百分比加成（%）。";
		case "mmpr":
			return "最大MP百分比加成（%）。";
		case "pddr":
			return "物理防御相关比率参数（百分比字段，具体按端实现，常见为防御率/减伤率）。";
		case "mddr":
			return "魔法防御相关比率参数（百分比字段，具体按端实现，常见为防御率/减伤率）。";
		case "pdr":
			return "物理伤害减免率（%）常见字段（部分端使用）。";
		case "mdr":
			return "魔法伤害减免率（%）常见字段（部分端使用）。";
		case "time":
		case "bufftime":
		case "duration":
			return "持续时间参数。多数配置按“秒”理解，部分实现会在读写时换算为毫秒。";
		case "damage":
			return "伤害百分比/系数参数（通常用于 #damage 文本占位）。";
		case "attackcount":
			return "攻击段数（一次释放可命中的次数）。";
		case "mobcount":
			return "最多命中目标数量。";
		case "range":
			return "技能作用范围参数（含义由技能类型决定）。";
		case "cooltime":
			return "冷却时间参数（多数端按秒或毫秒解释，取决于服务端读取逻辑）。";
		case "prop":
		case "cr":
		case "criticalrate":
			return "触发/暴击概率参数，常见范围 0~100（%）。";
		case "criticaldamage":
		case "criticaldmg":
			return "暴击伤害加成参数（通常为百分比）。";
		case "x":
		case "y":
		case "z":
			return "通用自定义参数，具体含义由技能逻辑决定。";
		case "u":
			return "通用参数字段 u（字段名）。注意它与公式函数 u(expr) 不同：u(expr) 表示向上取整。";
		case "d":
			return "通用参数字段 d（字段名）。注意它与公式函数 d(expr) 不同：d(expr) 表示向下取整。";
		default:
			if (text.EndsWith("r"))
			{
				return "比率/百分比参数（R=Rate），具体含义由字段名前缀决定。";
			}
			if (text.EndsWith("con"))
			{
				return "消耗参数（Con=Consume），通常表示释放时消耗。";
			}
			return "公共参数会写入 common 节点，可填写常量或表达式。";
		}
	}

	private static string GetFormulaSyntaxHelp()
	{
		return "公式语法：x=当前技能等级；u(expr)=向上取整(ceil)；d(expr)=向下取整(floor)；支持 + - * /。示例：6+2*u(x/5)。";
	}

	private string BuildParamCellHint(int rowIndex, int columnIndex)
	{
		if (dgvLevelParams == null || rowIndex < 0 || columnIndex < 0 || rowIndex >= dgvLevelParams.Rows.Count || columnIndex >= dgvLevelParams.Columns.Count)
		{
			return null;
		}
		if (IsCommonParamsGridMode())
		{
			string key = dgvLevelParams.Rows[rowIndex].Cells[0].Value?.ToString()?.Trim() ?? "";
			string help = GetCommonParamHelp(key);
			if (columnIndex == 0)
			{
				return string.IsNullOrEmpty(key)
					? $"公共参数名\n示例：maxLevel、mpCon、time、damage\n{GetFormulaSyntaxHelp()}"
					: $"参数名：{key}\n作用：{help}\n{GetFormulaSyntaxHelp()}";
			}
			return string.IsNullOrEmpty(key)
				? $"参数值/公式\n支持常量或表达式（例如 100+5*x）\n{GetFormulaSyntaxHelp()}"
				: $"参数值/公式（{key}）\n作用：{help}\n{GetFormulaSyntaxHelp()}";
		}
		if (columnIndex == 0)
		{
			return "等级编号\n表示这一行对应的技能等级";
		}
		string text = dgvLevelParams.Columns[columnIndex].Name;
		return $"等级参数：{text}\n该值只作用于当前等级";
	}

	private void DgvLevelParams_CellMouseEnter(object sender, DataGridViewCellEventArgs e)
	{
		ShowParamToolTip(e.RowIndex, e.ColumnIndex, -1, -1);
	}

	private void DgvLevelParams_MouseMove(object sender, MouseEventArgs e)
	{
		if (dgvLevelParams == null)
		{
			return;
		}
		var hit = dgvLevelParams.HitTest(e.X, e.Y);
		ShowParamToolTip(hit.RowIndex, hit.ColumnIndex, e.X, e.Y);
	}

	private void ShowParamToolTip(int rowIndex, int columnIndex, int mouseX, int mouseY)
	{
		if (_toolTip == null || dgvLevelParams == null)
		{
			return;
		}
		string text = BuildParamCellHint(rowIndex, columnIndex);
		if (string.IsNullOrWhiteSpace(text) || rowIndex < 0 || columnIndex < 0)
		{
			if (_lastParamTipRow != -2 || _lastParamTipCol != -2)
			{
				_toolTip.Hide(dgvLevelParams);
				_lastParamTipRow = -2;
				_lastParamTipCol = -2;
				_lastParamTipText = "";
			}
			return;
		}
		if (_lastParamTipRow == rowIndex && _lastParamTipCol == columnIndex && string.Equals(_lastParamTipText, text, StringComparison.Ordinal))
		{
			return;
		}
		_toolTip.Hide(dgvLevelParams);
		if (mouseX >= 0 && mouseY >= 0)
		{
			_toolTip.Show(text, dgvLevelParams, mouseX + 16, mouseY + 16, 15000);
		}
		else
		{
			Rectangle cellDisplayRectangle = dgvLevelParams.GetCellDisplayRectangle(columnIndex, rowIndex, cutOverflow: false);
			_toolTip.Show(text, dgvLevelParams, cellDisplayRectangle.Left + 12, cellDisplayRectangle.Bottom + 6, 15000);
		}
		_lastParamTipRow = rowIndex;
		_lastParamTipCol = columnIndex;
		_lastParamTipText = text;
	}

	private void DgvLevelParams_MouseLeave(object sender, EventArgs e)
	{
		if (_toolTip != null && dgvLevelParams != null)
		{
			_toolTip.Hide(dgvLevelParams);
		}
		_lastParamTipRow = -2;
		_lastParamTipCol = -2;
		_lastParamTipText = "";
	}

	private bool IsCarrierSkillId(int skillId)
	{
		if (skillId <= 0)
		{
			return false;
		}
		if (skillId == PathConfig.DefaultSuperSpCarrierSkillId)
		{
			return true;
		}
		HashSet<int> staleCarrierIds = CarrierSkillHelper.GetStaleCarrierIds();
		return staleCarrierIds.Contains(skillId);
	}

	private void WireCarrierSkillIdEditor(TextBox textBox)
	{
		if (textBox == null)
		{
			return;
		}
		textBox.Leave += CarrierSkillIdEditor_Leave;
		textBox.KeyDown += CarrierSkillIdEditor_KeyDown;
	}

	private void CarrierSkillIdEditor_KeyDown(object sender, KeyEventArgs e)
	{
		if (e.KeyCode == Keys.Return)
		{
			TryApplyCarrierSkillId(sender as TextBox, showError: true);
			e.SuppressKeyPress = true;
		}
	}

	private void CarrierSkillIdEditor_Leave(object sender, EventArgs e)
	{
		TryApplyCarrierSkillId(sender as TextBox, showError: false);
	}

	private void SyncCarrierSkillIdEditors(TextBox source = null)
	{
		if (_syncCarrierSkillText)
		{
			return;
		}
		_syncCarrierSkillText = true;
		try
		{
			string text = PathConfig.DefaultSuperSpCarrierSkillId.ToString();
			if (txtDefaultCarrierId != null && !object.ReferenceEquals(source, txtDefaultCarrierId))
			{
				txtDefaultCarrierId.Text = text;
			}
		}
		finally
		{
			_syncCarrierSkillText = false;
		}
	}

	private bool TryApplyCarrierSkillId(TextBox source, bool showError)
	{
		if (source == null)
		{
			SyncCarrierSkillIdEditors();
			return true;
		}
		string text = source.Text?.Trim();
		if (!int.TryParse(text, out var result) || result <= 0)
		{
			if (showError)
			{
				MessageBox.Show("请输入有效的载体技能ID", "提示");
			}
			SyncCarrierSkillIdEditors();
			return false;
		}
		if (result == PathConfig.DefaultSuperSpCarrierSkillId)
		{
			SyncCarrierSkillIdEditors(source);
			return true;
		}
		PushUndoSnapshot();
		int defaultSuperSpCarrierSkillId = PathConfig.DefaultSuperSpCarrierSkillId;
		PathConfig.RegisterCarrierSkillId(defaultSuperSpCarrierSkillId);
		PathConfig.DefaultSuperSpCarrierSkillId = result;
		PathConfig.RegisterCarrierSkillId(result);
		SettingsManager.Save();
		SyncCarrierSkillIdEditors(source);
		int num = RemapCarrierSkillOverridesInQueues(defaultSuperSpCarrierSkillId, result);
		int num2 = RemoveCarrierSkillsFromQueues(defaultSuperSpCarrierSkillId, result);
		if (num2 > 0)
		{
			RefreshListView();
		}
		if (num > 0 || num2 > 0)
			SavePendingList();
		if (num > 0)
			Console.WriteLine($"[GUI] 已同步 {num} 个技能的 superSpCarrierSkillId -> {result}");
		Console.WriteLine($"[GUI] 载体技能ID已更新为 {result}");
		return true;
	}

	private int RemapCarrierSkillOverridesInQueues(int oldCarrierSkillId, int newCarrierSkillId)
	{
		if (oldCarrierSkillId <= 0 || newCarrierSkillId <= 0 || oldCarrierSkillId == newCarrierSkillId)
		{
			return 0;
		}
		int num = RemapCarrierSkillOverridesInList(_pendingSkills, oldCarrierSkillId, newCarrierSkillId);
		int num2 = RemapCarrierSkillOverridesInList(_deletedSkills, oldCarrierSkillId, newCarrierSkillId);
		return num + num2;
	}

	private static int RemapCarrierSkillOverridesInList(List<SkillDefinition> list, int oldCarrierSkillId, int newCarrierSkillId)
	{
		if (list == null || list.Count == 0)
		{
			return 0;
		}
		int num = 0;
		foreach (SkillDefinition item in list)
		{
			if (item == null || item.SkillId == oldCarrierSkillId)
			{
				continue;
			}
			if (item.SuperSpCarrierSkillId == oldCarrierSkillId)
			{
				item.SuperSpCarrierSkillId = newCarrierSkillId;
				num++;
			}
		}
		return num;
	}

	private int RemoveCarrierSkillsFromQueues(params int[] carrierSkillIds)
	{
		HashSet<int> hashSet = new HashSet<int>();
		if (carrierSkillIds != null && carrierSkillIds.Length > 0)
		{
			int[] array = carrierSkillIds;
			foreach (int num in array)
			{
				if (num > 0)
				{
					hashSet.Add(num);
				}
			}
		}
		if (hashSet.Count == 0 && PathConfig.DefaultSuperSpCarrierSkillId > 0)
		{
			hashSet.Add(PathConfig.DefaultSuperSpCarrierSkillId);
		}
		if (carrierSkillIds == null || carrierSkillIds.Length == 0)
		{
			HashSet<int> staleCarrierIds = CarrierSkillHelper.GetStaleCarrierIds();
			foreach (int item in staleCarrierIds)
			{
				hashSet.Add(item);
			}
		}
		if (hashSet.Count == 0)
		{
			return 0;
		}
		int num2 = _pendingSkills.RemoveAll((SkillDefinition s) => s != null && hashSet.Contains(s.SkillId));
		int num3 = _deletedSkills.RemoveAll((SkillDefinition s) => s != null && hashSet.Contains(s.SkillId));
		if (num2 > 0)
		{
			_editState.EditingListIndex = null;
		}
		return num2 + num3;
	}

	private PictureBox MakeEditablePicBox(int x, int y)
	{
		PictureBox pb = new PictureBox
		{
			Location = new Point(x, y),
			Size = new Size(64, 64),
			SizeMode = PictureBoxSizeMode.Zoom,
			BorderStyle = BorderStyle.FixedSingle,
			BackColor = Color.FromArgb(40, 40, 40),
			AllowDrop = true
		};
		pb.Click += PicBox_Click;
		pb.DragEnter += PicBox_DragEnter;
		pb.DragDrop += PicBox_DragDrop;
		ContextMenuStrip contextMenuStrip = new ContextMenuStrip();
		contextMenuStrip.Items.Add("清除图标", null, delegate
		{
			pb.Image = null;
			SetIconOverride(pb, null);
		});
		pb.ContextMenuStrip = contextMenuStrip;
		return pb;
	}

	private void PicBox_Click(object sender, EventArgs e)
	{
		PictureBox pictureBox = (PictureBox)sender;
		OpenFileDialog openFileDialog = new OpenFileDialog
		{
			Filter = "图片文件|*.png;*.bmp;*.gif;*.jpg|全部文件|*.*"
		};
		try
		{
			if (openFileDialog.ShowDialog() != DialogResult.OK)
			{
				return;
			}
			try
			{
				Bitmap bmp = (Bitmap)(pictureBox.Image = new Bitmap(openFileDialog.FileName));
				SetIconOverride(pictureBox, bmp);
			}
			catch (Exception ex)
			{
				MessageBox.Show("加载图片失败: " + ex.Message);
			}
		}
		finally
		{
			((IDisposable)(object)openFileDialog)?.Dispose();
		}
	}

	private void PicBox_DragEnter(object sender, DragEventArgs e)
	{
		if (e.Data.GetDataPresent(DataFormats.FileDrop))
		{
			e.Effect = DragDropEffects.Copy;
		}
	}

	private void PicBox_DragDrop(object sender, DragEventArgs e)
	{
		string[] array = (string[])e.Data.GetData(DataFormats.FileDrop);
		if (array == null || array.Length == 0)
		{
			return;
		}
		try
		{
			PictureBox pictureBox = (PictureBox)sender;
			Bitmap bmp = (Bitmap)(pictureBox.Image = new Bitmap(array[0]));
			SetIconOverride(pictureBox, bmp);
		}
		catch (Exception ex)
		{
			MessageBox.Show("加载图片失败: " + ex.Message);
		}
	}

	private void SetIconOverride(PictureBox pb, Bitmap bmp)
	{
		if (!_isRestoringSnapshot)
		{
			PushUndoSnapshot();
		}

		if (pb == picIcon)
		{
			_editState.IconOverride?.Dispose();
			_editState.IconOverride = bmp;
		}
		else if (pb == picIconMO)
		{
			_editState.IconMOOverride?.Dispose();
			_editState.IconMOOverride = bmp;
		}
		else if (pb == picIconDis)
		{
			_editState.IconDisOverride?.Dispose();
			_editState.IconDisOverride = bmp;
		}
	}

	private void MainForm_KeyDown(object sender, KeyEventArgs e)
	{
		if (e == null || !e.Control || IsTextInputControl(base.ActiveControl))
		{
			return;
		}
		if (e.KeyCode == Keys.Z)
		{
			if (UndoQueueChange())
			{
				e.SuppressKeyPress = true;
			}
			return;
		}
		if (e.KeyCode == Keys.Y && RedoQueueChange())
		{
			e.SuppressKeyPress = true;
		}
	}

	private bool IsTextInputControl(Control control)
	{
		for (Control control2 = control; control2 != null; control2 = control2.Parent)
		{
			if (control2 is TextBoxBase)
			{
				return true;
			}
			if (control2 is ComboBox comboBox && comboBox.DropDownStyle != ComboBoxStyle.DropDownList)
			{
				return true;
			}
		}
		return false;
	}

	private void BtnUndo_Click(object sender, EventArgs e)
	{
		UndoQueueChange();
	}

	private void BtnRedo_Click(object sender, EventArgs e)
	{
		RedoQueueChange();
	}

	private void PushUndoSnapshot()
	{
		if (_isRestoringSnapshot)
		{
			return;
		}
		_undoStack.Push(CaptureQueueSnapshot());
		TrimHistory(_undoStack);
		_redoStack.Clear();
		UpdateUndoRedoButtons();
	}

	private void TrimHistory(Stack<SkillQueueSnapshot> history)
	{
		if (history == null || history.Count <= MaxUndoRedoDepth)
		{
			return;
		}
		SkillQueueSnapshot[] array = history.ToArray();
		history.Clear();
		int num = Math.Min(MaxUndoRedoDepth, array.Length);
		for (int num2 = num - 1; num2 >= 0; num2--)
		{
			history.Push(array[num2]);
		}
	}

	private SkillQueueSnapshot CaptureQueueSnapshot()
	{
		return new SkillQueueSnapshot
		{
			PendingSkills = CloneSkillList(_pendingSkills),
			DeletedSkills = CloneSkillList(_deletedSkills),
			EditingListIndex = _editState.EditingListIndex,
			EditState = CaptureEditStateSnapshot(),
			FormState = CaptureFormStateSnapshot()
		};
	}

	private List<SkillDefinition> CloneSkillList(List<SkillDefinition> source)
	{
		List<SkillDefinition> list = new List<SkillDefinition>();
		if (source == null)
		{
			return list;
		}
		foreach (SkillDefinition item in source)
		{
			list.Add(CloneSkillDefinition(item));
		}
		return list;
	}

	private SkillDefinition CloneSkillDefinition(SkillDefinition source)
	{
		if (source == null)
		{
			return null;
		}
		SkillDefinition skillDefinition = new SkillDefinition
		{
			SkillId = source.SkillId,
			Name = source.Name,
			Desc = source.Desc,
			PDesc = source.PDesc,
			Ph = source.Ph,
			Type = source.Type,
			Tab = source.Tab,
			MaxLevel = source.MaxLevel,
			SuperSpCost = source.SuperSpCost,
			Icon = source.Icon,
			IconMouseOver = source.IconMouseOver,
			IconDisabled = source.IconDisabled,
			IconBase64 = source.IconBase64,
			IconMouseOverBase64 = source.IconMouseOverBase64,
			IconDisabledBase64 = source.IconDisabledBase64,
			ReleaseType = source.ReleaseType,
			ReleaseClass = source.ReleaseClass,
			BorrowDonorVisual = source.BorrowDonorVisual,
			ProxySkillId = source.ProxySkillId,
			VisualSkillId = source.VisualSkillId,
			CloneFromSkillId = source.CloneFromSkillId,
			PreserveClonedNode = source.PreserveClonedNode,
			Action = source.Action,
			InfoType = source.InfoType,
			SourceLabel = source.SourceLabel,
			ExistsInImg = source.ExistsInImg,
			HideFromNativeSkillWnd = source.HideFromNativeSkillWnd,
			ShowInNativeWhenLearned = source.ShowInNativeWhenLearned,
			ShowInSuperWhenLearned = source.ShowInSuperWhenLearned,
			AllowNativeUpgradeFallback = source.AllowNativeUpgradeFallback,
			InjectToNative = source.InjectToNative,
			InjectEnabled = source.InjectEnabled,
			DonorSkillId = source.DonorSkillId,
			MountItemId = source.MountItemId,
			AllowMountedFlight = source.AllowMountedFlight,
			MountResourceMode = source.MountResourceMode,
			MountSourceItemId = source.MountSourceItemId,
			MountTamingMobId = source.MountTamingMobId,
			MountSpeedOverride = source.MountSpeedOverride,
			MountJumpOverride = source.MountJumpOverride,
			MountFatigueOverride = source.MountFatigueOverride,
			SuperSpCarrierSkillId = source.SuperSpCarrierSkillId,
			ServerEnabled = source.ServerEnabled,
			HasManualEffectOverride = source.HasManualEffectOverride
		};
		if (source.Common != null)
		{
			foreach (KeyValuePair<string, string> item in source.Common)
			{
				skillDefinition.Common[item.Key] = item.Value;
			}
		}
		if (source.RequiredSkills != null)
		{
			foreach (KeyValuePair<int, int> requiredSkill in source.RequiredSkills)
			{
				skillDefinition.RequiredSkills[requiredSkill.Key] = requiredSkill.Value;
			}
		}
		if (source.HLevels != null)
		{
			foreach (KeyValuePair<string, string> hLevel in source.HLevels)
			{
				skillDefinition.HLevels[hLevel.Key] = hLevel.Value;
			}
		}
		if (source.Levels != null)
		{
			skillDefinition.Levels = new Dictionary<int, Dictionary<string, string>>();
			foreach (KeyValuePair<int, Dictionary<string, string>> level in source.Levels)
			{
				skillDefinition.Levels[level.Key] = new Dictionary<string, string>(level.Value);
			}
		}
		if (source.CachedEffects != null)
		{
			skillDefinition.CachedEffects = new List<WzEffectFrame>();
			foreach (WzEffectFrame cachedEffect in source.CachedEffects)
			{
				WzEffectFrame cloned = WzEffectFrame.CloneShallowBitmap(cachedEffect);
				if (cloned != null)
				{
					skillDefinition.CachedEffects.Add(cloned);
				}
			}
		}
		if (source.CachedEffectsByNode != null && source.CachedEffectsByNode.Count > 0)
		{
			skillDefinition.CachedEffectsByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
			foreach (KeyValuePair<string, List<WzEffectFrame>> item2 in source.CachedEffectsByNode)
			{
				if (string.IsNullOrWhiteSpace(item2.Key) || item2.Value == null)
				{
					continue;
				}
				List<WzEffectFrame> list = new List<WzEffectFrame>();
				foreach (WzEffectFrame item3 in item2.Value)
				{
					WzEffectFrame wzEffectFrame = WzEffectFrame.CloneShallowBitmap(item3);
					if (wzEffectFrame != null)
					{
						list.Add(wzEffectFrame);
					}
				}
				if (list.Count > 0)
				{
					skillDefinition.CachedEffectsByNode[item2.Key] = list;
				}
			}
		}
		if (source.CachedTree != null)
		{
			skillDefinition.CachedTree = DeepCopyNode(source.CachedTree);
		}
		skillDefinition.NormalizeTextFields();
		return skillDefinition;
	}

	private bool UndoQueueChange()
	{
		if (_undoStack.Count == 0)
		{
			return false;
		}
		_redoStack.Push(CaptureQueueSnapshot());
		TrimHistory(_redoStack);
		RestoreQueueSnapshot(_undoStack.Pop());
		UpdateUndoRedoButtons();
		Console.WriteLine("[GUI] 已执行回退");
		return true;
	}

	private bool RedoQueueChange()
	{
		if (_redoStack.Count == 0)
		{
			return false;
		}
		_undoStack.Push(CaptureQueueSnapshot());
		TrimHistory(_undoStack);
		RestoreQueueSnapshot(_redoStack.Pop());
		UpdateUndoRedoButtons();
		Console.WriteLine("[GUI] 已执行恢复");
		return true;
	}

	private void RestoreQueueSnapshot(SkillQueueSnapshot snapshot)
	{
		_isRestoringSnapshot = true;
		try
		{
			_pendingSkills = CloneSkillList(snapshot?.PendingSkills);
			_deletedSkills = CloneSkillList(snapshot?.DeletedSkills);
			RemoveCarrierSkillsFromQueues();

			RestoreEditStateSnapshot(snapshot?.EditState);
			_editState.EditingListIndex = snapshot?.EditingListIndex;
			if (_editState.EditingListIndex.HasValue)
			{
				int value = _editState.EditingListIndex.Value;
				if (value < 0 || value >= _pendingSkills.Count)
				{
					_editState.EditingListIndex = null;
				}
			}

			RestoreFormStateSnapshot(snapshot?.FormState);
			RefreshListView();

			int selectedSkillIndex = snapshot?.FormState?.SelectedSkillIndex ?? -1;
			if (selectedSkillIndex >= 0 && selectedSkillIndex < lvSkills.Items.Count)
			{
				lvSkills.Items[selectedSkillIndex].Selected = true;
				lvSkills.Items[selectedSkillIndex].Focused = true;
			}
			else if (_editState.EditingListIndex.HasValue && _editState.EditingListIndex.Value < lvSkills.Items.Count)
			{
				lvSkills.Items[_editState.EditingListIndex.Value].Selected = true;
			}
			SavePendingList();
		}
		finally
		{
			_isRestoringSnapshot = false;
		}
	}

	private FormStateSnapshot CaptureFormStateSnapshot()
	{
		return new FormStateSnapshot
		{
			SkillId = txtSkillId?.Text ?? "",
			Name = txtName?.Text ?? "",
			Desc = txtDesc?.Text ?? "",
			TabIndex = (cboTab != null && cboTab.SelectedIndex >= 0) ? cboTab.SelectedIndex : 0,
			MaxLevel = nudMaxLevel?.Value ?? 1m,
			SuperSpCost = nudSuperSpCost?.Value ?? 1m,
			PacketRouteIndex = (cboPacketRoute != null && cboPacketRoute.SelectedIndex >= 0) ? cboPacketRoute.SelectedIndex : 0,
			ReleaseClassIndex = (cboReleaseClass != null && cboReleaseClass.SelectedIndex >= 0) ? cboReleaseClass.SelectedIndex : 0,
			ProxySkillId = txtProxySkillId?.Text ?? "",
			VisualSkillId = txtVisualSkillId?.Text ?? "",
			MountItemId = txtMountItemId?.Text ?? "",
			MountResourceModeIndex = (cboMountResourceMode != null && cboMountResourceMode.SelectedIndex >= 0) ? cboMountResourceMode.SelectedIndex : 0,
			MountSourceItemId = txtMountSourceItemId?.Text ?? "",
			MountTamingMobId = txtMountTamingMobId?.Text ?? "",
			MountSpeed = txtMountSpeed?.Text ?? "",
			MountJump = txtMountJump?.Text ?? "",
			MountFatigue = txtMountFatigue?.Text ?? "",
			BorrowDonorVisual = chkBorrowDonorVisual?.Checked ?? false,
			HideFromNative = chkHideFromNative?.Checked ?? false,
			ShowInNativeWhenLearned = chkShowInNativeWhenLearned?.Checked ?? false,
			ShowInSuperWhenLearned = chkShowInSuperWhenLearned?.Checked ?? false,
			AllowNativeFallback = chkAllowNativeFallback?.Checked ?? false,
			InjectToNative = chkInjectToNative?.Checked ?? false,
			AllowMountedFlight = chkAllowMountedFlight?.Checked ?? false,
			SelectedSkillIndex = (lvSkills != null && lvSkills.SelectedIndices.Count > 0) ? lvSkills.SelectedIndices[0] : -1,
			SelectedEffectIndex = (lvEffectFrames != null && lvEffectFrames.SelectedIndices.Count > 0) ? lvEffectFrames.SelectedIndices[0] : -1,
			SelectedEffectNodeName = (cboEffectNode?.SelectedItem as string) ?? (_editState?.SelectedEffectNodeName ?? "effect")
		};
	}

	private void RestoreFormStateSnapshot(FormStateSnapshot snapshot)
	{
		if (snapshot == null)
		{
			return;
		}

		if (txtSkillId != null) txtSkillId.Text = snapshot.SkillId ?? "";
		if (txtName != null) txtName.Text = snapshot.Name ?? "";
		if (txtDesc != null) txtDesc.Text = snapshot.Desc ?? "";
		if (cboTab != null && snapshot.TabIndex >= 0 && snapshot.TabIndex < cboTab.Items.Count) cboTab.SelectedIndex = snapshot.TabIndex;
		if (nudMaxLevel != null) nudMaxLevel.Value = Math.Min(nudMaxLevel.Maximum, Math.Max(nudMaxLevel.Minimum, snapshot.MaxLevel));
		if (nudSuperSpCost != null) nudSuperSpCost.Value = Math.Min(nudSuperSpCost.Maximum, Math.Max(nudSuperSpCost.Minimum, snapshot.SuperSpCost));
		if (cboPacketRoute != null && snapshot.PacketRouteIndex >= 0 && snapshot.PacketRouteIndex < cboPacketRoute.Items.Count) cboPacketRoute.SelectedIndex = snapshot.PacketRouteIndex;
		if (cboReleaseClass != null && snapshot.ReleaseClassIndex >= 0 && snapshot.ReleaseClassIndex < cboReleaseClass.Items.Count) cboReleaseClass.SelectedIndex = snapshot.ReleaseClassIndex;
		if (txtProxySkillId != null) txtProxySkillId.Text = snapshot.ProxySkillId ?? "";
		if (txtVisualSkillId != null) txtVisualSkillId.Text = snapshot.VisualSkillId ?? "";
		if (txtMountItemId != null) txtMountItemId.Text = snapshot.MountItemId ?? "";
		if (cboMountResourceMode != null && snapshot.MountResourceModeIndex >= 0 && snapshot.MountResourceModeIndex < cboMountResourceMode.Items.Count) cboMountResourceMode.SelectedIndex = snapshot.MountResourceModeIndex;
		if (txtMountSourceItemId != null) txtMountSourceItemId.Text = snapshot.MountSourceItemId ?? "";
		if (txtMountTamingMobId != null) txtMountTamingMobId.Text = snapshot.MountTamingMobId ?? "";
		if (txtMountSpeed != null) txtMountSpeed.Text = snapshot.MountSpeed ?? "";
		if (txtMountJump != null) txtMountJump.Text = snapshot.MountJump ?? "";
		if (txtMountFatigue != null) txtMountFatigue.Text = snapshot.MountFatigue ?? "";
		if (chkBorrowDonorVisual != null) chkBorrowDonorVisual.Checked = snapshot.BorrowDonorVisual;
		if (chkHideFromNative != null) chkHideFromNative.Checked = snapshot.HideFromNative;
		if (chkShowInNativeWhenLearned != null) chkShowInNativeWhenLearned.Checked = snapshot.ShowInNativeWhenLearned;
		if (chkShowInSuperWhenLearned != null) chkShowInSuperWhenLearned.Checked = snapshot.ShowInSuperWhenLearned;
		if (chkAllowNativeFallback != null) chkAllowNativeFallback.Checked = snapshot.AllowNativeFallback;
		if (chkInjectToNative != null) chkInjectToNative.Checked = snapshot.InjectToNative;
		if (chkAllowMountedFlight != null) chkAllowMountedFlight.Checked = snapshot.AllowMountedFlight;

		SafeSetImage(picIcon, _editState.GetEffectiveIcon());
		SafeSetImage(picIconMO, _editState.GetEffectiveIconMO());
		SafeSetImage(picIconDis, _editState.GetEffectiveIconDis());

		WzNodeInfo treeRoot = _editState.EditedTree ?? _editState.LoadedData?.RootNode;
		if (treeRoot != null)
		{
			PopulateTreeView(treeRoot);
		}
		else
		{
			if (treeSkillData != null)
			{
				treeSkillData.Nodes.Clear();
			}
		}

		RefreshEffectNodeSelector(snapshot.SelectedEffectNodeName, createIfMissing: true);
		PopulateEffectFrames(_editState.EditedEffects);
		if (lvEffectFrames != null && snapshot.SelectedEffectIndex >= 0 && snapshot.SelectedEffectIndex < lvEffectFrames.Items.Count)
		{
			lvEffectFrames.Items[snapshot.SelectedEffectIndex].Selected = true;
			lvEffectFrames.Items[snapshot.SelectedEffectIndex].Focused = true;
		}

		WzSkillData paramData = BuildParamDisplayData();
		if (paramData != null)
		{
			PopulateSkillParams(paramData);
		}

		if (lblAction != null)
		{
			string action = _editState.LoadedData?.Action ?? "";
			lblAction.Text = string.IsNullOrEmpty(action) ? "" : ("动作: " + action);
		}
	}

	private WzSkillData BuildParamDisplayData()
	{
		var data = new WzSkillData();

		if (_editState.LoadedData != null)
		{
			data.SkillId = _editState.LoadedData.SkillId;
			data.JobId = _editState.LoadedData.JobId;
		}

		if (_editState.EditedLevelParams != null && _editState.EditedLevelParams.Count > 0)
		{
			if (_editState.EditedLevelParams.TryGetValue(0, out var common))
			{
				data.CommonParams = new Dictionary<string, string>(common);
			}

			bool hasLevel = _editState.EditedLevelParams.Keys.Any((int k) => k >= 1);
			if (hasLevel)
			{
				data.LevelParams = new Dictionary<int, Dictionary<string, string>>();
				foreach (var kv in _editState.EditedLevelParams)
				{
					if (kv.Key >= 1)
						data.LevelParams[kv.Key] = new Dictionary<string, string>(kv.Value);
				}
			}
			return data;
		}

		if (_editState.LoadedData != null)
		{
			data.CommonParams = _editState.LoadedData.CommonParams != null
				? new Dictionary<string, string>(_editState.LoadedData.CommonParams)
				: null;
			data.LevelParams = _editState.LoadedData.LevelParams != null
				? CloneLevelParams(_editState.LoadedData.LevelParams)
				: null;
			return data;
		}

		return null;
	}

	private EditStateSnapshot CaptureEditStateSnapshot()
	{
		var snapshot = new EditStateSnapshot
		{
			LoadedData = CaptureWzSkillDataSnapshot(_editState.LoadedData),
			IconOverrideBase64 = EditState.BitmapToBase64(_editState.IconOverride),
			IconMOOverrideBase64 = EditState.BitmapToBase64(_editState.IconMOOverride),
			IconDisOverrideBase64 = EditState.BitmapToBase64(_editState.IconDisOverride),
			EditedTree = (_editState.EditedTree != null) ? DeepCopyNode(_editState.EditedTree) : null,
			EditedLevelParams = CloneLevelParams(_editState.EditedLevelParams) ?? new Dictionary<int, Dictionary<string, string>>(),
			SelectedEffectNodeName = _editState.SelectedEffectNodeName ?? "effect",
			HasManualEffectEdit = _hasManualEffectEdit
		};

		if (_editState.EditedEffectsByNode != null && _editState.EditedEffectsByNode.Count > 0)
		{
			foreach (KeyValuePair<string, List<WzEffectFrame>> item in _editState.EditedEffectsByNode)
			{
				if (string.IsNullOrWhiteSpace(item.Key) || item.Value == null)
				{
					continue;
				}
				List<EffectFrameSnapshot> list = new List<EffectFrameSnapshot>();
				foreach (WzEffectFrame item2 in item.Value)
				{
					EffectFrameSnapshot effectFrameSnapshot = CaptureEffectFrameSnapshot(item2);
					if (effectFrameSnapshot != null)
					{
						list.Add(effectFrameSnapshot);
					}
				}
				if (list.Count > 0)
				{
					snapshot.EditedEffectsByNode[item.Key] = list;
				}
			}
		}
		else if (_editState.EditedEffects != null)
		{
			foreach (WzEffectFrame frame in _editState.EditedEffects)
			{
				EffectFrameSnapshot frameSnapshot = CaptureEffectFrameSnapshot(frame);
				if (frameSnapshot != null)
				{
					snapshot.EditedEffects.Add(frameSnapshot);
				}
			}
		}

		return snapshot;
	}

	private void RestoreEditStateSnapshot(EditStateSnapshot snapshot)
	{
		_editState.Clear();
		_hasManualEffectEdit = false;

		if (snapshot == null)
		{
			return;
		}

		_editState.LoadedData = RestoreWzSkillDataSnapshot(snapshot.LoadedData);
		if (!string.IsNullOrEmpty(snapshot.IconOverrideBase64))
			_editState.IconOverride = EditState.BitmapFromBase64(snapshot.IconOverrideBase64);
		if (!string.IsNullOrEmpty(snapshot.IconMOOverrideBase64))
			_editState.IconMOOverride = EditState.BitmapFromBase64(snapshot.IconMOOverrideBase64);
		if (!string.IsNullOrEmpty(snapshot.IconDisOverrideBase64))
			_editState.IconDisOverride = EditState.BitmapFromBase64(snapshot.IconDisOverrideBase64);

		_editState.EditedTree = snapshot.EditedTree != null
			? DeepCopyNode(snapshot.EditedTree)
			: _editState.LoadedData?.RootNode;
		_editState.EditedLevelParams = CloneLevelParams(snapshot.EditedLevelParams);
		_editState.EditedEffectsByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
		if (snapshot.EditedEffectsByNode != null && snapshot.EditedEffectsByNode.Count > 0)
		{
			foreach (KeyValuePair<string, List<EffectFrameSnapshot>> item in snapshot.EditedEffectsByNode)
			{
				if (string.IsNullOrWhiteSpace(item.Key) || item.Value == null)
				{
					continue;
				}
				List<WzEffectFrame> list = new List<WzEffectFrame>();
				foreach (EffectFrameSnapshot item2 in item.Value)
				{
					WzEffectFrame wzEffectFrame = RestoreEffectFrameSnapshot(item2);
					if (wzEffectFrame != null)
					{
						list.Add(wzEffectFrame);
					}
				}
				if (list.Count > 0)
				{
					_editState.EditedEffectsByNode[item.Key] = list;
				}
			}
		}
		else if (snapshot.EditedEffects != null)
		{
			List<WzEffectFrame> list2 = new List<WzEffectFrame>();
			foreach (EffectFrameSnapshot frameSnapshot in snapshot.EditedEffects)
			{
				WzEffectFrame restored = RestoreEffectFrameSnapshot(frameSnapshot);
				if (restored != null)
				{
					list2.Add(restored);
				}
			}
			if (list2.Count > 0)
			{
				_editState.EditedEffectsByNode["effect"] = list2;
			}
		}
		_editState.SetSelectedEffectNode(snapshot.SelectedEffectNodeName ?? "effect", createIfMissing: true);
		_hasManualEffectEdit = snapshot.HasManualEffectEdit;
	}

	private WzSkillDataSnapshot CaptureWzSkillDataSnapshot(WzSkillData source)
	{
		if (source == null)
		{
			return null;
		}

		return new WzSkillDataSnapshot
		{
			SkillId = source.SkillId,
			JobId = source.JobId,
			Name = source.Name ?? "",
			Desc = source.Desc ?? "",
			PDesc = source.PDesc ?? "",
			Ph = source.Ph ?? "",
			Action = source.Action ?? "",
			InfoType = source.InfoType,
			HLevels = source.HLevels != null ? new Dictionary<string, string>(source.HLevels) : new Dictionary<string, string>(),
			LevelParams = CloneLevelParams(source.LevelParams) ?? new Dictionary<int, Dictionary<string, string>>(),
			CommonParams = source.CommonParams != null ? new Dictionary<string, string>(source.CommonParams) : new Dictionary<string, string>(),
			IconBase64 = !string.IsNullOrEmpty(source.IconBase64) ? source.IconBase64 : EditState.BitmapToBase64(source.IconBitmap),
			IconMouseOverBase64 = !string.IsNullOrEmpty(source.IconMouseOverBase64) ? source.IconMouseOverBase64 : EditState.BitmapToBase64(source.IconMouseOverBitmap),
			IconDisabledBase64 = !string.IsNullOrEmpty(source.IconDisabledBase64) ? source.IconDisabledBase64 : EditState.BitmapToBase64(source.IconDisabledBitmap),
			RootNode = source.RootNode != null ? DeepCopyNode(source.RootNode) : null
		};
	}

	private WzSkillData RestoreWzSkillDataSnapshot(WzSkillDataSnapshot snapshot)
	{
		if (snapshot == null)
		{
			return null;
		}

		var data = new WzSkillData
		{
			SkillId = snapshot.SkillId,
			JobId = snapshot.JobId,
			Name = snapshot.Name ?? "",
			Desc = snapshot.Desc ?? "",
			PDesc = snapshot.PDesc ?? "",
			Ph = snapshot.Ph ?? "",
			Action = snapshot.Action ?? "",
			InfoType = snapshot.InfoType,
			HLevels = snapshot.HLevels != null ? new Dictionary<string, string>(snapshot.HLevels) : new Dictionary<string, string>(),
			LevelParams = CloneLevelParams(snapshot.LevelParams) ?? new Dictionary<int, Dictionary<string, string>>(),
			CommonParams = snapshot.CommonParams != null ? new Dictionary<string, string>(snapshot.CommonParams) : new Dictionary<string, string>(),
			IconBase64 = snapshot.IconBase64 ?? "",
			IconMouseOverBase64 = snapshot.IconMouseOverBase64 ?? "",
			IconDisabledBase64 = snapshot.IconDisabledBase64 ?? "",
			RootNode = snapshot.RootNode != null ? DeepCopyNode(snapshot.RootNode) : null
		};

		if (!string.IsNullOrEmpty(data.IconBase64))
			data.IconBitmap = EditState.BitmapFromBase64(data.IconBase64);
		if (!string.IsNullOrEmpty(data.IconMouseOverBase64))
			data.IconMouseOverBitmap = EditState.BitmapFromBase64(data.IconMouseOverBase64);
		if (!string.IsNullOrEmpty(data.IconDisabledBase64))
			data.IconDisabledBitmap = EditState.BitmapFromBase64(data.IconDisabledBase64);

		return data;
	}

	private EffectFrameSnapshot CaptureEffectFrameSnapshot(WzEffectFrame frame)
	{
		if (frame == null)
		{
			return null;
		}

		return new EffectFrameSnapshot
		{
			Index = frame.Index,
			Width = frame.Width,
			Height = frame.Height,
			Delay = frame.Delay,
			BitmapBase64 = EditState.BitmapToBase64(frame.Bitmap),
			Vectors = WzEffectFrame.CloneVectors(frame.Vectors),
			FrameProps = WzEffectFrame.CloneFrameProps(frame.FrameProps)
		};
	}

	private WzEffectFrame RestoreEffectFrameSnapshot(EffectFrameSnapshot snapshot)
	{
		if (snapshot == null)
		{
			return null;
		}

		Bitmap bmp = string.IsNullOrEmpty(snapshot.BitmapBase64)
			? null
			: EditState.BitmapFromBase64(snapshot.BitmapBase64);

		return new WzEffectFrame
		{
			Index = snapshot.Index,
			Width = snapshot.Width > 0 ? snapshot.Width : (bmp?.Width ?? 0),
			Height = snapshot.Height > 0 ? snapshot.Height : (bmp?.Height ?? 0),
			Delay = snapshot.Delay,
			Bitmap = bmp,
			Vectors = WzEffectFrame.CloneVectors(snapshot.Vectors),
			FrameProps = WzEffectFrame.CloneFrameProps(snapshot.FrameProps)
		};
	}

	private static Dictionary<int, Dictionary<string, string>> CloneLevelParams(Dictionary<int, Dictionary<string, string>> source)
	{
		if (source == null)
		{
			return null;
		}

		var result = new Dictionary<int, Dictionary<string, string>>();
		foreach (var kv in source)
		{
			result[kv.Key] = kv.Value != null
				? new Dictionary<string, string>(kv.Value)
				: new Dictionary<string, string>();
		}
		return result;
	}

	private void UpdateUndoRedoButtons()
	{
		if (btnUndo != null)
		{
			btnUndo.Enabled = _undoStack.Count > 0;
		}
		if (btnRedo != null)
		{
			btnRedo.Enabled = _redoStack.Count > 0;
		}
	}

	private int RemoveDeletedQueueBySkillId(int skillId)
	{
		return _deletedSkills.RemoveAll((SkillDefinition s) => s.SkillId == skillId);
	}

	private void QueueOldSkillWhenIdChanged(SkillDefinition oldSkill, SkillDefinition newSkill)
	{
		if (oldSkill == null || newSkill == null)
		{
			return;
		}
		if (oldSkill.SkillId <= 0 || newSkill.SkillId <= 0 || oldSkill.SkillId == newSkill.SkillId)
		{
			return;
		}
		QueueDeleteSkill(oldSkill);
		Console.WriteLine($"[GUI] 技能ID已变更：{oldSkill.SkillId} -> {newSkill.SkillId}，已将旧ID加入删除队列");
	}

	private void QueueDeleteSkill(SkillDefinition skill)
	{
		_deletedSkills ??= new List<SkillDefinition>();
		if (skill == null)
		{
			return;
		}
		if (IsNativeSkillDeleteProtected(skill))
		{
			Console.WriteLine($"[GUI] 跳过删除队列：{skill.SkillId} ({skill.Name}) 为原版技能，仅从待执行列表移除。");
			return;
		}
		if (!_deletedSkills.Any((SkillDefinition s) => s != null && s.SkillId == skill.SkillId))
		{
			_deletedSkills.Add(CloneSkillDefinition(skill));
		}
	}

	private int QueueDeleteSkills(IEnumerable<SkillDefinition> skills)
	{
		int num = 0;
		if (skills == null)
		{
			return 0;
		}
		_deletedSkills ??= new List<SkillDefinition>();
		foreach (SkillDefinition skill in skills)
		{
			if (skill == null)
			{
				continue;
			}
			if (IsNativeSkillDeleteProtected(skill))
			{
				Console.WriteLine($"[GUI] 跳过删除队列：{skill.SkillId} ({skill.Name}) 为原版技能，仅从待执行列表移除。");
				continue;
			}
			if (!_deletedSkills.Any((SkillDefinition s) => s != null && s.SkillId == skill.SkillId))
			{
				_deletedSkills.Add(CloneSkillDefinition(skill));
				num++;
			}
		}
		return num;
	}

	private bool IsNativeSkillDeleteProtected(SkillDefinition skill)
	{
		if (skill == null || skill.SkillId <= 0)
		{
			return false;
		}

		// Fast path from queued metadata.
		if (skill.ExistsInImg && string.Equals(skill.SourceLabel ?? "", "原生技能", StringComparison.OrdinalIgnoreCase))
		{
			return true;
		}

		// Safety fallback: query current .img marker.
		try
		{
			if (_wzLoader != null && _wzLoader.SkillExistsInImg(skill.SkillId) && !_wzLoader.IsSuperSkill(skill.SkillId))
			{
				return true;
			}
		}
		catch
		{
		}
		return false;
	}

	private void SyncSkillIdFromJobAndNum()
	{
		if (_suppressIdSync)
		{
			return;
		}
		_suppressIdSync = true;
		try
		{
			int selectedIndex = cboJobId.SelectedIndex;
			if (selectedIndex >= 0 && selectedIndex < JobList.Count)
			{
				int value = JobList[selectedIndex].Value;
				int result = 0;
				int.TryParse(txtSkillNum.Text.Trim(), out result);
				txtSkillIdInput.Text = (value * 10000 + result).ToString();
			}
		}
		finally
		{
			_suppressIdSync = false;
		}
	}

	private void SyncJobAndNumFromSkillId()
	{
		if (_suppressIdSync)
		{
			return;
		}
		_suppressIdSync = true;
		try
		{
			if (!int.TryParse(txtSkillIdInput.Text.Trim(), out var result) || result < 0)
			{
				return;
			}
			int num = result / 10000;
			int num2 = result % 10000;
			txtSkillNum.Text = num2.ToString();
			for (int i = 0; i < JobList.Count; i++)
			{
				if (JobList[i].Value == num)
				{
					cboJobId.SelectedIndex = i;
					break;
				}
			}
		}
		finally
		{
			_suppressIdSync = false;
		}
	}

	private static void SafeSetImage(PictureBox pic, Bitmap bmp)
	{
		if (pic == null)
		{
			return;
		}
		Image image = pic.Image;
		pic.Image = null;
		if (image != null)
		{
			try
			{
				image.Dispose();
			}
			catch
			{
			}
		}
		if (bmp == null)
		{
			return;
		}
		try
		{
			int width = bmp.Width;
			int height = bmp.Height;
			Bitmap image2 = new Bitmap(width, height, PixelFormat.Format32bppArgb);
			using (Graphics graphics = Graphics.FromImage(image2))
			{
				graphics.DrawImage(bmp, 0, 0, width, height);
			}
			pic.Image = image2;
		}
		catch
		{
			pic.Image = null;
		}
	}

	private static int ResolveMaxLevelFromWzData(WzSkillData data, int fallback = 20)
	{
		if (data != null && data.CommonParams != null)
		{
			foreach (KeyValuePair<string, string> commonParam in data.CommonParams)
			{
				if (string.Equals(commonParam.Key, "maxLevel", StringComparison.OrdinalIgnoreCase)
					&& int.TryParse(commonParam.Value, out var parsed)
					&& parsed > 0)
				{
					return parsed;
				}
			}
		}
		if (data != null && data.LevelParams != null && data.LevelParams.Count > 0)
		{
			return data.LevelParams.Count;
		}
		return fallback;
	}

	private int? TryGetSelectedJobId()
	{
		if (cboJobId == null)
			return null;

		int selectedIndex = cboJobId.SelectedIndex;
		if (selectedIndex < 0 || selectedIndex >= JobList.Count)
			return null;

		return JobList[selectedIndex].Value;
	}

	private bool TryLoadSkillWithInputFallback(
		int inputSkillId,
		int? preferredJobId,
		out WzSkillData loadedData,
		out int resolvedSkillId,
		out string triedText)
	{
		loadedData = null;
		resolvedSkillId = inputSkillId;

		List<int> candidates = new List<int>();
		HashSet<int> seen = new HashSet<int>();
		void AddCandidate(int value)
		{
			if (value <= 0)
				return;
			if (seen.Add(value))
				candidates.Add(value);
		}

		AddCandidate(inputSkillId);

		if (inputSkillId > 0 && inputSkillId < 10000)
		{
			if (preferredJobId.HasValue && preferredJobId.Value > 0)
				AddCandidate(preferredJobId.Value * 10000 + inputSkillId);

			int? selectedJobId = TryGetSelectedJobId();
			if (selectedJobId.HasValue && selectedJobId.Value > 0)
				AddCandidate(selectedJobId.Value * 10000 + inputSkillId);
		}

		List<string> tried = new List<string>();
		Exception lastNotFound = null;

		foreach (int candidate in candidates)
		{
			try
			{
				loadedData = _wzLoader.LoadSkill(candidate, preferredJobId);
				resolvedSkillId = candidate;
				triedText = string.Join(", ", tried);
				return true;
			}
			catch (KeyNotFoundException ex)
			{
				lastNotFound = ex;
				tried.Add(candidate.ToString());
			}
		}

		triedText = string.Join(", ", tried);
		if (lastNotFound != null)
			throw new KeyNotFoundException(
				$"{lastNotFound.Message}（已尝试: {triedText}）",
				lastNotFound);
		return false;
	}

	private void DoLoadSkill(int? preferredJobId = null)
	{
		if (!int.TryParse(txtSkillIdInput.Text.Trim(), out var result) || result <= 0)
		{
			lblLoadStatus.Text = "无效的技能ID";
			lblLoadStatus.ForeColor = Color.Red;
			return;
		}
		try
		{
			if (!TryLoadSkillWithInputFallback(result, preferredJobId, out WzSkillData wzSkillData, out int resolvedSkillId, out string _))
				return;
			result = resolvedSkillId;
			PushUndoSnapshot();
			_editState.LoadFromSkillData(wzSkillData);
			_hasManualEffectEdit = false;
			RefreshAnimLevelSelector(preserveSelection: false);
			RefreshEffectNodeSelector("effect", createIfMissing: true);
			int value = wzSkillData.JobId;
			lblLoadStatus.Text = $"已加载 skill/{result}（来自 {PathConfig.SkillImgName(value)}）";
			lblLoadStatus.ForeColor = Color.LimeGreen;
			SafeSetImage(picIcon, wzSkillData.IconBitmap);
			SafeSetImage(picIconMO, wzSkillData.IconMouseOverBitmap);
			SafeSetImage(picIconDis, wzSkillData.IconDisabledBitmap);
			txtSkillId.Text = result.ToString();
			txtSkillIdInput.Text = result.ToString();
			txtName.Text = wzSkillData.Name ?? "";
			txtDesc.Text = wzSkillData.Desc ?? "";
			int loadedMaxLevel = ResolveMaxLevelFromWzData(wzSkillData, 20);
			nudMaxLevel.Value = Math.Min(Math.Max(loadedMaxLevel, 1), 30);
			cboTab.SelectedIndex = ((wzSkillData.InfoType == 50) ? 1 : 0);
			lblAction.Text = (string.IsNullOrEmpty(wzSkillData.Action) ? "" : ("动作: " + wzSkillData.Action));
			AutoFillRoute(wzSkillData, result);
			SkillDefinition skillDefinition = new SkillDefinition
			{
				SkillId = result,
				ReleaseType = ((cboPacketRoute.SelectedIndex >= 0 && cboPacketRoute.SelectedIndex < RouteNames.Length) ? RouteNames[cboPacketRoute.SelectedIndex] : ""),
				ProxySkillId = (int.TryParse(txtProxySkillId.Text.Trim(), out var result2) ? result2 : 0),
				VisualSkillId = (int.TryParse(txtVisualSkillId.Text.Trim(), out var result3) ? result3 : 0),
				ReleaseClass = ((cboReleaseClass.SelectedItem as string) ?? ReleaseClassNames[0]),
				HideFromNativeSkillWnd = chkHideFromNative.Checked,
				ShowInNativeWhenLearned = chkShowInNativeWhenLearned.Checked,
				ShowInSuperWhenLearned = chkShowInSuperWhenLearned.Checked,
				AllowNativeUpgradeFallback = chkAllowNativeFallback.Checked,
				InjectToNative = chkInjectToNative.Checked,
				AllowMountedFlight = chkAllowMountedFlight?.Checked ?? false
			};
			ApplyConfigHints(skillDefinition);
			EnsureRecommendedRoute(skillDefinition, wzSkillData);
			if (!string.IsNullOrEmpty(skillDefinition.ReleaseType))
			{
				int num = Array.IndexOf(RouteNames, skillDefinition.ReleaseType);
				if (num >= 0)
				{
					cboPacketRoute.SelectedIndex = num;
				}
			}
			txtProxySkillId.Text = ((skillDefinition.ProxySkillId > 0) ? skillDefinition.ProxySkillId.ToString() : "");
			txtVisualSkillId.Text = ((skillDefinition.VisualSkillId > 0) ? skillDefinition.VisualSkillId.ToString() : "");
			txtMountItemId.Text = ((skillDefinition.MountItemId > 0) ? skillDefinition.MountItemId.ToString() : "");
			SetSelectedMountResourceMode(skillDefinition.MountResourceMode);
			int mountSourceHint = ResolveMountSourceHint(skillDefinition);
			txtMountSourceItemId.Text = ((skillDefinition.MountSourceItemId > 0) ? skillDefinition.MountSourceItemId.ToString() : ((mountSourceHint > 0) ? mountSourceHint.ToString() : ""));
			txtMountTamingMobId.Text = ((skillDefinition.MountTamingMobId > 0) ? skillDefinition.MountTamingMobId.ToString() : "");
			txtMountSpeed.Text = (skillDefinition.MountSpeedOverride.HasValue ? skillDefinition.MountSpeedOverride.Value.ToString() : "");
			txtMountJump.Text = (skillDefinition.MountJumpOverride.HasValue ? skillDefinition.MountJumpOverride.Value.ToString() : "");
			txtMountFatigue.Text = (skillDefinition.MountFatigueOverride.HasValue ? skillDefinition.MountFatigueOverride.Value.ToString() : "");
			chkBorrowDonorVisual.Checked = skillDefinition.BorrowDonorVisual;
			int num2 = Array.IndexOf(ReleaseClassNames, skillDefinition.ReleaseClass ?? "");
			cboReleaseClass.SelectedIndex = ((num2 >= 0) ? num2 : 0);
			chkHideFromNative.Checked = skillDefinition.HideFromNativeSkillWnd;
			chkShowInNativeWhenLearned.Checked = skillDefinition.ShowInNativeWhenLearned;
			chkShowInSuperWhenLearned.Checked = skillDefinition.ShowInSuperWhenLearned;
			chkAllowNativeFallback.Checked = skillDefinition.AllowNativeUpgradeFallback;
			chkInjectToNative.Checked = skillDefinition.InjectToNative;
			if (chkAllowMountedFlight != null)
			{
				chkAllowMountedFlight.Checked = skillDefinition.AllowMountedFlight;
			}
			TrySyncMountEditorFromSkill(skillDefinition);
			PopulateTreeView(wzSkillData.RootNode);
			PopulateSkillParams(wzSkillData);
			PopulateEffectFrames(_editState.EditedEffects);
			RefreshAnimLevelSelector(preserveSelection: false);
			PopulateTextFields();
			Console.WriteLine($"[GUI] 已加载 skill/{result} (图标:{((wzSkillData.IconBitmap != null) ? "有" : "无")}, 通用参数:{wzSkillData.CommonParams?.Count ?? 0}, 等级数:{wzSkillData.LevelParams?.Count ?? 0}, 特效节点:{wzSkillData.EffectFramesByNode?.Count ?? 0}, 当前节点帧:{_editState.EditedEffects?.Count ?? 0}, 等级动画:{wzSkillData.LevelAnimFramesByNode?.Count ?? 0})");
		}
		catch (Exception ex)
		{
			lblLoadStatus.Text = ex.Message;
			lblLoadStatus.ForeColor = Color.Red;
			Console.WriteLine("[GUI] 加载失败: " + ex.Message);
			Console.WriteLine("[GUI] 加载失败堆栈: " + ex);
		}
	}

	private void AutoFillRoute(WzSkillData data, int skillId)
	{
		int num = data?.InfoType ?? 0;
		if (num == 50 || ContainsPassiveSignals(data))
		{
			grpRoute.Enabled = false;
			return;
		}
		grpRoute.Enabled = true;
		string text = InferPacketRouteByQuickRules(data);
		if (string.Equals(text, "special_move", StringComparison.OrdinalIgnoreCase))
		{
			cboPacketRoute.SelectedIndex = 3;
		}
		else
		{
			cboPacketRoute.SelectedIndex = 0;
		}
		txtProxySkillId.Text = skillId.ToString();
	}

	private void EnsureRecommendedRoute(SkillDefinition sd, WzSkillData data)
	{
		if (sd == null)
		{
			return;
		}
		if (sd.InfoType == 50 || ContainsPassiveSignals(data))
		{
			sd.ReleaseType = "";
			return;
		}
		if (!_routesCfgById.ContainsKey(sd.SkillId))
		{
			string text = GetDonorPacketRoute(sd);
			if (string.IsNullOrEmpty(text))
			{
				text = InferPacketRouteByQuickRules(data);
			}
			if (string.IsNullOrEmpty(text))
			{
				text = "close_range";
			}
			sd.ReleaseType = text;
		}
	}

	private string GetDonorPacketRoute(SkillDefinition sd)
	{
		if (sd == null)
		{
			return "";
		}
		int num = ((sd.DonorSkillId > 0) ? sd.DonorSkillId : sd.ProxySkillId);
		if (num <= 0)
		{
			return "";
		}
		if (_routesCfgById.TryGetValue(num, out var value))
		{
			return SimpleJson.GetString(value, "packetRoute", "");
		}
		return "";
	}

	private static bool ContainsPassiveSignals(WzSkillData data)
	{
		if (data == null)
		{
			return false;
		}
		if (HasNodeName(data.RootNode, "psd") || HasNodeName(data.RootNode, "psdSkill"))
		{
			return true;
		}
		if (data.CommonParams != null)
		{
			if (data.CommonParams.ContainsKey("psd") || data.CommonParams.ContainsKey("psdSkill"))
			{
				return true;
			}
		}
		return false;
	}

	private static string InferPacketRouteByQuickRules(WzSkillData data)
	{
		if (data == null)
		{
			return "close_range";
		}
		if (data.InfoType == 50 || ContainsPassiveSignals(data))
		{
			return "";
		}
		bool flag3 = ContainsSpecialMoveDescSignal(data.Desc);
		bool flag = false;
		bool flag2 = false;
		if (!string.IsNullOrEmpty(data.Action))
		{
			flag = string.Equals(data.Action, "alert2", StringComparison.OrdinalIgnoreCase);
			flag2 = string.Equals(data.Action, "fly", StringComparison.OrdinalIgnoreCase);
		}
		bool flag4 = HasNodeName(data.RootNode, "vehicleID") || HasNodeName(data.RootNode, "ride");
		bool flag5 = HasNodeName(data.RootNode, "morph");
		bool flag6 = data.InfoType == 33 || HasNodeName(data.RootNode, "summon") || HasNodeName(data.RootNode, "minionAttack");
		if (flag2 || flag3 || flag4 || flag5 || flag6 || flag || data.InfoType == 2 || data.InfoType == 10)
		{
			return "special_move";
		}
		if (data.InfoType == 1)
		{
			return "close_range";
		}
		return "close_range";
	}

	private static bool ContainsSpecialMoveDescSignal(string desc)
	{
		if (string.IsNullOrWhiteSpace(desc))
		{
			return false;
		}
		// 骑乘/飞行类技能描述常见关键词：命中后优先 special_move。
		string[] keywords = new string[12]
		{
			"有可以骑着",
			"可以骑着",
			"骑乘",
			"坐骑",
			"搭乘",
			"飞行",
			"翱翔",
			"滑翔",
			"fly",
			"soar",
			"mount",
			"vehicle"
		};
		foreach (string keyword in keywords)
		{
			if (desc.IndexOf(keyword, StringComparison.OrdinalIgnoreCase) >= 0)
			{
				return true;
			}
		}
		return false;
	}

	private static bool HasNodeName(WzNodeInfo node, string nodeName)
	{
		if (node == null || string.IsNullOrEmpty(nodeName))
		{
			return false;
		}
		if (string.Equals(node.Name, nodeName, StringComparison.OrdinalIgnoreCase))
		{
			return true;
		}
		if (node.Children == null || node.Children.Count == 0)
		{
			return false;
		}
		foreach (WzNodeInfo child in node.Children)
		{
			if (HasNodeName(child, nodeName))
			{
				return true;
			}
		}
		return false;
	}

	private void BtnLoadVisual_Click(object sender, EventArgs e)
	{
		if (!int.TryParse(txtVisualSkillId.Text.Trim(), out var result) || result <= 0)
		{
			MessageBox.Show("请输入有效的外观技能ID", "提示");
			return;
		}
		try
		{
			WzSkillData wzSkillData = _wzLoader.LoadSkillVisuals(result);
			PushUndoSnapshot();
			if (wzSkillData.IconBitmap != null)
			{
				SafeSetImage(picIcon, wzSkillData.IconBitmap);
				_editState.IconOverride = wzSkillData.IconBitmap;
			}
			if (wzSkillData.IconMouseOverBitmap != null)
			{
				SafeSetImage(picIconMO, wzSkillData.IconMouseOverBitmap);
				_editState.IconMOOverride = wzSkillData.IconMouseOverBitmap;
			}
			if (wzSkillData.IconDisabledBitmap != null)
			{
				SafeSetImage(picIconDis, wzSkillData.IconDisabledBitmap);
				_editState.IconDisOverride = wzSkillData.IconDisabledBitmap;
			}
			Dictionary<string, List<WzEffectFrame>> dictionary = EditState.CloneEffectsByNode(wzSkillData.EffectFramesByNode);
			if ((dictionary == null || dictionary.Count == 0) && wzSkillData.EffectFrames != null && wzSkillData.EffectFrames.Count > 0)
			{
				dictionary = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase)
				{
					["effect"] = EditState.CloneEffectFrameList(wzSkillData.EffectFrames)
				};
			}
			if (dictionary != null && dictionary.Count > 0)
			{
				_editState.EditedEffectsByNode = dictionary;
				_hasManualEffectEdit = true;
				RefreshEffectNodeSelector("effect", createIfMissing: true);
				PopulateEffectFrames(_editState.EditedEffects);
			}
			Console.WriteLine($"[GUI] 已加载外观资源，来源 skill/{result}");
		}
		catch (Exception ex)
		{
			MessageBox.Show("加载外观失败: " + ex.Message);
		}
	}

	private void PopulateTreeView(WzNodeInfo root)
	{
		treeSkillData.Nodes.Clear();
		if (root != null)
		{
			TreeNode node = BuildTreeNode(root);
			treeSkillData.Nodes.Add(node);
			treeSkillData.ExpandAll();
			CollapseDeep(treeSkillData.Nodes, 0, 3);
		}
	}

	private TreeNode BuildTreeNode(WzNodeInfo info)
	{
		string text = (string.IsNullOrEmpty(info.Value) ? ("[" + info.TypeName + "] " + info.Name) : $"[{info.TypeName}] {info.Name} = {info.Value}");
		TreeNode treeNode = new TreeNode(text);
		treeNode.Tag = info;
		if (info.Children != null)
		{
			foreach (WzNodeInfo child in info.Children)
			{
				treeNode.Nodes.Add(BuildTreeNode(child));
			}
		}
		return treeNode;
	}

	private void CollapseDeep(TreeNodeCollection nodes, int depth, int maxDepth)
	{
		foreach (TreeNode node in nodes)
		{
			if (depth >= maxDepth)
			{
				node.Collapse();
			}
			else
			{
				CollapseDeep(node.Nodes, depth + 1, maxDepth);
			}
		}
	}

	private void PopulateSkillParams(WzSkillData data)
	{
		dgvLevelParams.Columns.Clear();
		dgvLevelParams.Rows.Clear();
		if (data.CommonParams != null && data.CommonParams.Count > 0)
		{
			_lblParamType.Text = "公式参数（公共节点，可编辑）：";
			dgvLevelParams.Columns.Add("Param", "参数");
			dgvLevelParams.Columns.Add("Formula/Value", "公式/值");
			List<string> list = new List<string>(data.CommonParams.Keys);
			list.Sort();
			foreach (string item in list)
			{
				dgvLevelParams.Rows.Add(item, data.CommonParams[item]);
			}
			if (_editState.EditedLevelParams == null)
			{
				_editState.EditedLevelParams = new Dictionary<int, Dictionary<string, string>>();
			}
			_editState.EditedLevelParams[0] = new Dictionary<string, string>(data.CommonParams);
		}
		else if (data.LevelParams != null && data.LevelParams.Count > 0)
		{
			_lblParamType.Text = $"按等级参数（{data.LevelParams.Count}级，可编辑）：";
			PopulateLevelParamsGrid(_editState.EditedLevelParams);
		}
		else
		{
			_lblParamType.Text = "技能参数（无数据）：";
		}
	}

	private void PopulateLevelParamsGrid(Dictionary<int, Dictionary<string, string>> levelParams)
	{
		dgvLevelParams.Columns.Clear();
		dgvLevelParams.Rows.Clear();
		if (levelParams == null || levelParams.Count == 0)
		{
			return;
		}
		HashSet<string> hashSet = new HashSet<string>();
		foreach (KeyValuePair<int, Dictionary<string, string>> levelParam in levelParams)
		{
			foreach (string key in levelParam.Value.Keys)
			{
				if (!string.IsNullOrEmpty(levelParam.Value[key]))
				{
					hashSet.Add(key);
				}
			}
		}
		if (hashSet.Count == 0)
		{
			return;
		}
		List<string> list = new List<string>(hashSet);
		list.Sort();
		dgvLevelParams.Columns.Add("等级", "等级");
		foreach (string item in list)
		{
			dgvLevelParams.Columns.Add(item, item);
		}
		List<int> list2 = new List<int>(levelParams.Keys);
		list2.Sort();
		foreach (int item2 in list2)
		{
			bool flag = false;
			foreach (string item3 in list)
			{
				if (levelParams[item2].TryGetValue(item3, out var value) && !string.IsNullOrEmpty(value))
				{
					flag = true;
					break;
				}
			}
			if (!flag)
			{
				continue;
			}
			List<string> list3 = new List<string> { item2.ToString() };
			foreach (string item4 in list)
			{
				list3.Add(levelParams[item2].TryGetValue(item4, out var value2) ? value2 : "");
			}
			DataGridViewRowCollection rows = dgvLevelParams.Rows;
			object[] values = list3.ToArray();
			rows.Add(values);
		}
	}

	private static bool TryParseIndexedEffectNodeName(string name, string prefix, out int index)
	{
		index = -1;
		if (string.IsNullOrWhiteSpace(name) || string.IsNullOrWhiteSpace(prefix))
		{
			return false;
		}
		if (!name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
		{
			return false;
		}
		string text = name.Substring(prefix.Length);
		if (string.IsNullOrEmpty(text))
		{
			return false;
		}
		return int.TryParse(text, out index);
	}

	private static readonly (string baseName, int baseRank, bool canIndex)[] EffectNodeSortTable = new[]
	{
		("effect",     0, true),
		("repeat",     2, true),
		("ball",       4, true),
		("hit",        6, false),
		("prepare",    8, false),
		("keydown",   10, true),
		("keydownend",12, false),
		("affected",  14, true),
		("mob",       16, true),
		("special",   18, true),
		("screen",    20, false),
		("tile",      22, true),
		("finish",    24, true),
	};

	private static int CompareEffectNodeNames(string a, string b)
	{
		if (string.Equals(a, b, StringComparison.OrdinalIgnoreCase))
			return 0;
		int aRank = GetEffectNodeSortRank(a, out var aIndex);
		int bRank = GetEffectNodeSortRank(b, out var bIndex);
		if (aRank != bRank)
			return aRank.CompareTo(bRank);
		if (aIndex != int.MaxValue || bIndex != int.MaxValue)
			return aIndex.CompareTo(bIndex);
		return string.Compare(a, b, StringComparison.OrdinalIgnoreCase);
	}

	private static int GetEffectNodeSortRank(string name, out int index)
	{
		index = int.MaxValue;
		if (string.IsNullOrWhiteSpace(name))
			return 99;

		if (name.StartsWith("hit/", StringComparison.OrdinalIgnoreCase))
		{
			if (int.TryParse(name.Substring(4), out index))
				return 6;
			return 6;
		}
		if (string.Equals(name, "hit", StringComparison.OrdinalIgnoreCase))
		{
			index = -1;
			return 6;
		}

		foreach (var (baseName, baseRank, canIndex) in EffectNodeSortTable)
		{
			if (baseName == "hit") continue;
			if (string.Equals(name, baseName, StringComparison.OrdinalIgnoreCase))
				return baseRank;
			if (canIndex && TryParseIndexedEffectNodeName(name, baseName, out index))
				return baseRank + 1;
		}
		return 99;
	}

	private List<WzEffectFrame> GetActiveEffectFrames(bool createIfMissing)
	{
		if (_editState == null)
		{
			return null;
		}
		List<WzEffectFrame> selectedEffectFrames = _editState.GetSelectedEffectFrames(createIfMissing);
		_editState.EditedEffects = selectedEffectFrames;
		return selectedEffectFrames;
	}

	private void SetActiveEffectNode(string nodeName, bool createIfMissing)
	{
		_editState.SetSelectedEffectNodeForActiveLevel(nodeName, createIfMissing);
	}

	private void RefreshEffectNodeSelector(string preferredNode = null, bool createIfMissing = true)
	{
		if (_editState == null || cboEffectNode == null)
		{
			return;
		}

		List<string> effectNodeNames = _editState.GetActiveAnimNodeNames();
		if (effectNodeNames.Count == 0 && createIfMissing)
		{
			_editState.SetSelectedEffectNodeForActiveLevel("effect", createIfMissing: true);
			effectNodeNames = _editState.GetActiveAnimNodeNames();
		}
		effectNodeNames.Sort(CompareEffectNodeNames);

		string text = !string.IsNullOrWhiteSpace(preferredNode)
			? EditState.NormalizeEffectNodeName(preferredNode)
			: EditState.NormalizeEffectNodeName(_editState.SelectedEffectNodeName);
		if (effectNodeNames.Count > 0 && !effectNodeNames.Any((string n) => string.Equals(n, text, StringComparison.OrdinalIgnoreCase)))
		{
			text = effectNodeNames[0];
		}
		if (effectNodeNames.Count == 0)
		{
			text = EditState.NormalizeEffectNodeName(text);
		}

		_suppressEffectNodeChange = true;
		try
		{
			cboEffectNode.BeginUpdate();
			cboEffectNode.Items.Clear();
			foreach (string item in effectNodeNames)
			{
				cboEffectNode.Items.Add(item);
			}
			if (effectNodeNames.Count > 0)
			{
				cboEffectNode.SelectedItem = effectNodeNames.FirstOrDefault((string n) => string.Equals(n, text, StringComparison.OrdinalIgnoreCase)) ?? effectNodeNames[0];
			}
			else
			{
				cboEffectNode.Text = "";
			}
		}
		finally
		{
			cboEffectNode.EndUpdate();
			_suppressEffectNodeChange = false;
		}

		if (effectNodeNames.Count > 0)
		{
			SetActiveEffectNode((string)cboEffectNode.SelectedItem, createIfMissing: false);
		}
		else
		{
			_editState.EditedEffects = null;
		}
	}

	private void CboEffectNode_SelectedIndexChanged(object sender, EventArgs e)
	{
		if (_suppressEffectNodeChange || cboEffectNode == null || _editState == null)
		{
			return;
		}
		string nodeName = cboEffectNode.SelectedItem as string;
		if (string.IsNullOrWhiteSpace(nodeName))
		{
			return;
		}
		SetActiveEffectNode(nodeName, createIfMissing: false);
		PopulateEffectFrames(_editState.EditedEffects);
	}

	private void CboAnimLevel_SelectedIndexChanged(object sender, EventArgs e)
	{
		if (_suppressAnimLevelChange || cboAnimLevel == null || _editState == null)
			return;
		int idx = cboAnimLevel.SelectedIndex;
		if (idx <= 0)
		{
			_editState.SetSelectedAnimLevel(null);
		}
		else
		{
			var levels = _editState.GetLevelsWithAnimFrames();
			if (idx - 1 < levels.Count)
				_editState.SetSelectedAnimLevel(levels[idx - 1]);
			else
				_editState.SetSelectedAnimLevel(null);
		}
		RefreshEffectNodeSelector(null, createIfMissing: false);
		PopulateEffectFrames(_editState.EditedEffects);
	}

	/// <summary>
	/// All known animation node families with Chinese descriptions.
	/// canIndex: supports indexed siblings (effect0, effect1...).
	/// canSubGroup: supports slash sub-groups (effect/0, effect/1...).
	/// Both may be true — the actual pattern is determined by existing data.
	/// </summary>
	private static readonly (string baseName, string desc, bool canIndex, bool canSubGroup)[] AnimNodeFamilies = new[]
	{
		("effect",   "技能特效",       true,  true ),
		("ball",     "弹道/飞行物",    true,  true ),
		("hit",      "命中效果",       false, true ),
		("repeat",   "循环特效",       true,  false),
		("prepare",  "预备/蓄力前摇",  false, false),
		("keydown",  "按键持续效果",   true,  false),
		("keydownend","按键结束效果",   false, false),
		("affected", "状态覆盖动画",   true,  true ),
		("mob",      "怪物相关特效",   true,  false),
		("special",  "特殊效果",       true,  true ),
		("screen",   "全屏效果",       false, false),
		("tile",     "地面效果",       true,  true ),
		("finish",   "结束动画",       true,  true ),
	};

	/// <summary>
	/// Build a smart list of suggested node names based on what already exists.
	/// Rules:
	/// - If "X/N" sub-groups exist, only suggest more "X/N+1" sub-groups (not X0 indexed siblings)
	/// - If "X" exists as direct frames, suggest indexed siblings "X0" (not X/0 sub-groups)
	/// - If nothing exists for this family, suggest the base name first
	/// - If "X0" indexed siblings exist, don't suggest X/0 sub-groups
	/// </summary>
	private static List<(string key, string label)> BuildSmartNodeSuggestions(HashSet<string> existing)
	{
		var suggestions = new List<(string key, string label)>();

		foreach (var (baseName, desc, canIndex, canSubGroup) in AnimNodeFamilies)
		{
			// Detect which pattern is already in use for this family
			bool hasSubGroups = false;   // has baseName/N keys
			bool hasBase = existing.Contains(baseName); // has base name as direct-frame node
			bool hasIndexed = false;     // has baseNameN keys (effect0, effect1...)

			foreach (var key in existing)
			{
				if (key.StartsWith(baseName + "/", StringComparison.OrdinalIgnoreCase))
				{
					hasSubGroups = true;
				}
				else if (key.Length > baseName.Length
					&& key.StartsWith(baseName, StringComparison.OrdinalIgnoreCase)
					&& !key.Equals(baseName, StringComparison.OrdinalIgnoreCase)
					&& int.TryParse(key.Substring(baseName.Length), out _))
				{
					hasIndexed = true;
				}
			}

			if (hasSubGroups)
			{
				// Sub-group pattern active: only suggest next sub-group
				// Do NOT suggest base or indexed siblings
				if (canSubGroup)
				{
					for (int i = 0; i <= 9; i++)
					{
						string key = baseName + "/" + i;
						if (!existing.Contains(key))
						{
							suggestions.Add((key, $"{key} — {desc}(第{i + 1}组)"));
							break;
						}
					}
				}
				continue;
			}

			if (hasBase || hasIndexed)
			{
				// Direct-frame or indexed pattern active
				// Can add more indexed siblings, but NOT sub-groups
				if (canIndex && !hasSubGroups)
				{
					for (int i = 0; i <= 9; i++)
					{
						string key = baseName + i;
						if (!existing.Contains(key))
						{
							suggestions.Add((key, $"{key} — {desc}(第{i + 1}组)"));
							break;
						}
					}
				}
				continue;
			}

			// Nothing exists for this family yet — offer base name
			suggestions.Add((baseName, $"{baseName} — {desc}"));

			// Also offer sub-group start if the family supports it
			if (canSubGroup)
			{
				suggestions.Add((baseName + "/0", $"{baseName}/0 — {desc}(子分组模式)"));
			}
		}

		return suggestions;
	}

	/// <summary>
	/// Check if the proposed node name conflicts with existing naming patterns.
	/// Returns an error message if there's a conflict, null if OK.
	/// Rules:
	/// - If sub-groups (X/N) exist for family X, cannot add indexed sibling (XN) or direct base (X)
	/// - If direct base (X) or indexed siblings (XN) exist, cannot add sub-groups (X/N)
	/// </summary>
	private static string ValidateAnimNodeConflict(string proposed, HashSet<string> existing)
	{
		// Determine the family base name of the proposed node
		string familyBase = null;
		bool proposedIsSubGroup = false;   // X/N
		bool proposedIsIndexed = false;    // XN

		if (proposed.Contains("/"))
		{
			// Proposed is sub-group like "effect/0"
			familyBase = proposed.Substring(0, proposed.IndexOf('/'));
			proposedIsSubGroup = true;
		}
		else
		{
			// Check if it's an indexed name like "effect0"
			foreach (var (baseName, _, canIndex, _) in AnimNodeFamilies)
			{
				if (canIndex && proposed.Length > baseName.Length
					&& proposed.StartsWith(baseName, StringComparison.OrdinalIgnoreCase)
					&& int.TryParse(proposed.Substring(baseName.Length), out _))
				{
					familyBase = baseName;
					proposedIsIndexed = true;
					break;
				}
				if (string.Equals(proposed, baseName, StringComparison.OrdinalIgnoreCase))
				{
					familyBase = baseName;
					break;
				}
			}
		}

		if (familyBase == null)
			return null; // Unknown family, no conflict rules apply

		// Check existing nodes for conflicting patterns
		bool existingHasSubGroups = false;
		bool existingHasBase = existing.Contains(familyBase);
		bool existingHasIndexed = false;

		foreach (var key in existing)
		{
			if (key.StartsWith(familyBase + "/", StringComparison.OrdinalIgnoreCase))
				existingHasSubGroups = true;
			else if (key.Length > familyBase.Length
				&& key.StartsWith(familyBase, StringComparison.OrdinalIgnoreCase)
				&& !key.Equals(familyBase, StringComparison.OrdinalIgnoreCase)
				&& int.TryParse(key.Substring(familyBase.Length), out _))
				existingHasIndexed = true;
		}

		if (proposedIsSubGroup && (existingHasBase || existingHasIndexed))
		{
			return $"已存在 \"{familyBase}\" 的直接帧或索引节点（如 {familyBase}0），"
				 + $"不能同时添加子分组节点 \"{proposed}\"。\n\n"
				 + $"如果需要子分组模式，请先删除现有的 {familyBase} 系列节点。";
		}

		if ((proposedIsIndexed || string.Equals(proposed, familyBase, StringComparison.OrdinalIgnoreCase))
			&& existingHasSubGroups)
		{
			return $"已存在 \"{familyBase}\" 的子分组节点（如 {familyBase}/0），"
				 + $"不能同时添加直接帧或索引节点 \"{proposed}\"。\n\n"
				 + $"如果需要索引模式，请先删除现有的 {familyBase}/N 系列节点。";
		}

		return null;
	}

	private void BtnAddAnimNode_Click(object sender, EventArgs e)
	{
		if (_editState == null) return;
		var existing = new HashSet<string>(_editState.GetActiveAnimNodeNames(), StringComparer.OrdinalIgnoreCase);
		var suggestions = BuildSmartNodeSuggestions(existing);

		// Build picker dialog
		using var form = new Form
		{
			Text = "添加动画节点",
			ClientSize = new Size(380, 145),
			StartPosition = FormStartPosition.CenterParent,
			FormBorderStyle = FormBorderStyle.FixedDialog,
			MaximizeBox = false,
			MinimizeBox = false
		};
		form.Controls.Add(new Label { Text = "选择节点类型:", Location = new Point(10, 12), AutoSize = true });
		var combo = new ComboBox
		{
			Location = new Point(10, 34),
			Width = 360,
			DropDownStyle = ComboBoxStyle.DropDownList
		};
		foreach (var item in suggestions)
			combo.Items.Add(item.label);
		combo.Items.Add("── 自定义名称 ──");
		combo.SelectedIndex = 0;
		form.Controls.Add(combo);

		var txtCustom = new TextBox
		{
			Location = new Point(10, 64),
			Width = 360,
			PlaceholderText = "输入自定义节点名（如 effect2, hit/3, flipBall 等）",
			Enabled = false
		};
		form.Controls.Add(txtCustom);
		combo.SelectedIndexChanged += (s, ev) =>
		{
			txtCustom.Enabled = combo.SelectedIndex == combo.Items.Count - 1;
			if (txtCustom.Enabled) txtCustom.Focus();
		};

		var btnOK = new Button { Text = "确定", Location = new Point(200, 105), Width = 80, DialogResult = DialogResult.OK };
		var btnCancel = new Button { Text = "取消", Location = new Point(290, 105), Width = 80, DialogResult = DialogResult.Cancel };
		form.Controls.AddRange(new Control[] { btnOK, btnCancel });
		form.AcceptButton = btnOK;
		form.CancelButton = btnCancel;

		if (form.ShowDialog() != DialogResult.OK) return;

		string selected;
		if (combo.SelectedIndex == combo.Items.Count - 1)
		{
			selected = txtCustom.Text?.Trim().ToLowerInvariant();
			if (string.IsNullOrWhiteSpace(selected)) return;
		}
		else if (combo.SelectedIndex >= 0 && combo.SelectedIndex < suggestions.Count)
		{
			selected = suggestions[combo.SelectedIndex].key;
		}
		else return;

		if (existing.Contains(selected))
		{
			MessageBox.Show($"节点 \"{selected}\" 已存在。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
			return;
		}

		// Validate naming conflict rules for custom input
		string conflictMsg = ValidateAnimNodeConflict(selected, existing);
		if (conflictMsg != null)
		{
			MessageBox.Show(conflictMsg, "命名冲突", MessageBoxButtons.OK, MessageBoxIcon.Warning);
			return;
		}

		PushUndoSnapshot();
		_editState.AddAnimNode(selected);
		_hasManualEffectEdit = true;
		RefreshEffectNodeSelector(selected, createIfMissing: false);
		PopulateEffectFrames(_editState.EditedEffects);
	}

	private void BtnDeleteAnimNode_Click(object sender, EventArgs e)
	{
		if (_editState == null) return;
		string current = _editState.SelectedEffectNodeName;
		if (string.IsNullOrWhiteSpace(current)) return;
		if (MessageBox.Show($"确定删除动画节点 \"{current}\" 及其所有帧吗？", "确认删除",
			MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
			return;
		PushUndoSnapshot();
		_editState.DeleteAnimNode(current);
		_hasManualEffectEdit = true;
		RefreshEffectNodeSelector(null, createIfMissing: false);
		PopulateEffectFrames(_editState.EditedEffects);
	}

	private void BtnAddAnimLevel_Click(object sender, EventArgs e)
	{
		if (_editState == null) return;
		var existing = _editState.GetLevelsWithAnimFrames();
		int nextLevel = existing.Count > 0 ? existing.Max() + 1 : 1;
		string input = ShowInputBox("添加等级动画", $"输入等级编号 (1~30):", nextLevel.ToString());
		if (string.IsNullOrWhiteSpace(input)) return;
		if (!int.TryParse(input.Trim(), out int level) || level < 1 || level > 30)
		{
			MessageBox.Show("等级必须是 1~30 的整数。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Warning);
			return;
		}
		if (existing.Contains(level))
		{
			MessageBox.Show($"等级 {level} 已存在动画数据。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
			return;
		}
		PushUndoSnapshot();
		if (_editState.EditedLevelAnimFramesByNode == null)
			_editState.EditedLevelAnimFramesByNode = new Dictionary<int, Dictionary<string, List<WzEffectFrame>>>();
		_editState.EditedLevelAnimFramesByNode[level] = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase)
		{
			["effect"] = new List<WzEffectFrame>()
		};
		_hasManualEffectEdit = true;
		RefreshAnimLevelSelector(preserveSelection: false);
		// Select the newly added level
		_editState.SetSelectedAnimLevel(level);
		_suppressAnimLevelChange = true;
		try
		{
			var levels = _editState.GetLevelsWithAnimFrames();
			int idx = levels.IndexOf(level);
			if (idx >= 0)
				cboAnimLevel.SelectedIndex = idx + 1;
		}
		finally
		{
			_suppressAnimLevelChange = false;
		}
		RefreshEffectNodeSelector("effect", createIfMissing: true);
		PopulateEffectFrames(_editState.EditedEffects);
	}

	private void BtnRemoveAnimLevel_Click(object sender, EventArgs e)
	{
		if (_editState == null) return;
		int? current = _editState.SelectedAnimLevel;
		if (!current.HasValue)
		{
			MessageBox.Show("顶层(共享)不能删除，请先选择一个等级。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
			return;
		}
		if (MessageBox.Show($"确定删除等级 {current.Value} 的所有动画帧吗？", "确认删除",
			MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
			return;
		PushUndoSnapshot();
		if (_editState.EditedLevelAnimFramesByNode != null)
			_editState.EditedLevelAnimFramesByNode.Remove(current.Value);
		_editState.SetSelectedAnimLevel(null);
		_hasManualEffectEdit = true;
		RefreshAnimLevelSelector(preserveSelection: false);
		RefreshEffectNodeSelector(null, createIfMissing: false);
		PopulateEffectFrames(_editState.EditedEffects);
	}

	private void RefreshAnimLevelSelector(bool preserveSelection = true)
	{
		if (cboAnimLevel == null || _editState == null) return;
		int? prevLevel = preserveSelection ? _editState.SelectedAnimLevel : null;
		_suppressAnimLevelChange = true;
		try
		{
			cboAnimLevel.BeginUpdate();
			cboAnimLevel.Items.Clear();
			cboAnimLevel.Items.Add("顶层(共享)");
			var levels = _editState.GetLevelsWithAnimFrames();
			foreach (int lv in levels)
			{
				cboAnimLevel.Items.Add($"等级 {lv}");
			}
			if (prevLevel.HasValue && levels.Contains(prevLevel.Value))
			{
				cboAnimLevel.SelectedIndex = levels.IndexOf(prevLevel.Value) + 1;
			}
			else
			{
				// If top-level has no anim nodes but per-level does, auto-select first level
				bool topLevelHasAnims = _editState.EditedEffectsByNode != null && _editState.EditedEffectsByNode.Count > 0;
				if (!topLevelHasAnims && levels.Count > 0)
					cboAnimLevel.SelectedIndex = 1; // first per-level entry
				else
					cboAnimLevel.SelectedIndex = 0;
			}
		}
		finally
		{
			cboAnimLevel.EndUpdate();
			_suppressAnimLevelChange = false;
		}
		// Sync editState to match the selected combo item
		int selIdx = cboAnimLevel.SelectedIndex;
		if (selIdx <= 0)
		{
			_editState.SetSelectedAnimLevel(null);
		}
		else
		{
			var lvs = _editState.GetLevelsWithAnimFrames();
			if (selIdx - 1 < lvs.Count)
				_editState.SetSelectedAnimLevel(lvs[selIdx - 1]);
			else
				_editState.SetSelectedAnimLevel(null);
		}
	}

	private void PopulateTextFields()
	{
		if (_editState == null) return;
		if (txtHTemplate != null) txtHTemplate.Text = _editState.EditedH ?? "";
		if (txtPDesc != null) txtPDesc.Text = _editState.EditedPDesc ?? "";
		if (txtPh != null) txtPh.Text = _editState.EditedPh ?? "";
		PopulateHLevelsGrid();
		UpdateTextModeLabel();
	}

	private void UpdateTextModeLabel()
	{
		if (lblTextMode == null) return;
		var hLevels = _editState?.LoadedData?.HLevels;
		bool hasH = !string.IsNullOrWhiteSpace(_editState?.EditedH);
		bool hasPDesc = !string.IsNullOrWhiteSpace(_editState?.EditedPDesc);
		bool hasPh = !string.IsNullOrWhiteSpace(_editState?.EditedPh);
		bool hasHLevels = hLevels != null && hLevels.Count > 0;
		if (hasH && !hasHLevels)
			lblTextMode.Text = "技能文本: [模板模式 — 通用 h]";
		else if (hasHLevels && !hasH)
			lblTextMode.Text = $"技能文本: [等级模式 — h1~h{hLevels.Count}]";
		else if (hasH && hasHLevels)
			lblTextMode.Text = "技能文本: [混合模式 — h + h1~hN]";
		else
			lblTextMode.Text = "技能文本:";

		// Update tab 1 title: show only fields that have content
		if (tabTextEdit != null && tabTextEdit.TabPages.Count > 0)
		{
			var parts = new List<string>();
			if (hasH) parts.Add("h");
			if (hasPDesc) parts.Add("pdesc");
			if (hasPh) parts.Add("ph");
			tabTextEdit.TabPages[0].Text = parts.Count > 0
				? $"通用文本 ({string.Join("/", parts)})"
				: "通用文本";
		}

		// Update tab 2 title: show h-level range, truncate with ...
		if (tabTextEdit != null && tabTextEdit.TabPages.Count > 1)
		{
			if (hasHLevels)
			{
				var keys = hLevels.Keys
					.OrderBy(k => { string n = k.StartsWith("h", StringComparison.OrdinalIgnoreCase) ? k.Substring(1) : k; return int.TryParse(n, out int v) ? v : 9999; })
					.ToList();
				string summary;
				if (keys.Count <= 3)
					summary = string.Join("/", keys);
				else
					summary = $"{keys[0]}/{keys[1]}/{keys[2]}...";
				tabTextEdit.TabPages[1].Text = $"等级描述 ({summary})";
			}
			else
			{
				tabTextEdit.TabPages[1].Text = "等级描述";
			}
		}
	}

	private void PopulateHLevelsGrid()
	{
		if (dgvHLevels == null) return;
		dgvHLevels.Rows.Clear();
		var hLevels = _editState?.LoadedData?.HLevels;
		if (hLevels != null)
		{
			// Sort by numeric suffix so h1 < h2 < ... < h10 < h20
			var sorted = hLevels.OrderBy(k =>
			{
				string numPart = k.Key.StartsWith("h", StringComparison.OrdinalIgnoreCase) ? k.Key.Substring(1) : k.Key;
				return int.TryParse(numPart, out int n) ? n : 9999;
			}).ToList();
			foreach (var kv in sorted)
			{
				dgvHLevels.Rows.Add(kv.Key, kv.Value);
			}
		}
	}

	private void ClearTextFields()
	{
		if (txtHTemplate != null) txtHTemplate.Text = "";
		if (txtPDesc != null) txtPDesc.Text = "";
		if (txtPh != null) txtPh.Text = "";
		if (dgvHLevels != null) dgvHLevels.Rows.Clear();
		if (lblTextMode != null) lblTextMode.Text = "技能文本:";
	}

	private void CollectTextFieldsIntoEditState()
	{
		if (_editState == null) return;
		_editState.EditedH = txtHTemplate?.Text?.Trim() ?? "";
		_editState.EditedPDesc = txtPDesc?.Text?.Trim() ?? "";
		_editState.EditedPh = txtPh?.Text?.Trim() ?? "";
	}

	private Dictionary<string, string> CollectHLevelsFromGrid()
	{
		if (dgvHLevels == null) return new Dictionary<string, string>();
		var result = new Dictionary<string, string>();
		foreach (DataGridViewRow row in dgvHLevels.Rows)
		{
			if (row.IsNewRow) continue;
			string key = row.Cells["Key"].Value?.ToString()?.Trim() ?? "";
			string val = row.Cells["Value"].Value?.ToString() ?? "";
			if (!string.IsNullOrEmpty(key))
				result[key] = val;
		}
		return result;
	}

	// ── hLevels grid right-click menu handlers ──

	private int GetNextHLevelNumber()
	{
		int max = 0;
		if (dgvHLevels == null) return 1;
		foreach (DataGridViewRow row in dgvHLevels.Rows)
		{
			if (row.IsNewRow) continue;
			string key = row.Cells["Key"].Value?.ToString()?.Trim() ?? "";
			if (key.StartsWith("h", StringComparison.OrdinalIgnoreCase))
			{
				if (int.TryParse(key.Substring(1), out int n) && n > max)
					max = n;
			}
		}
		return max + 1;
	}

	private void HLevelsMenu_AddRow(object sender, EventArgs e)
	{
		if (dgvHLevels == null) return;
		int next = GetNextHLevelNumber();
		dgvHLevels.Rows.Add($"h{next}", "");
		// Select the new row for immediate editing
		int newIdx = dgvHLevels.Rows.Count - 2; // -2 because AllowUserToAddRows adds a blank row
		if (newIdx >= 0 && newIdx < dgvHLevels.Rows.Count)
		{
			dgvHLevels.CurrentCell = dgvHLevels.Rows[newIdx].Cells["Value"];
			dgvHLevels.BeginEdit(true);
		}
	}

	private void HLevelsMenu_BatchAdd(object sender, EventArgs e)
	{
		if (dgvHLevels == null || _editState == null) return;

		// Determine max level from level params
		int maxLevel = 1;
		if (_editState.EditedLevelParams != null)
		{
			foreach (var lv in _editState.EditedLevelParams.Keys)
			{
				if (lv > maxLevel) maxLevel = lv;
			}
		}
		// Also check common params for maxLevel
		if (_editState.LoadedData?.CommonParams != null
			&& _editState.LoadedData.CommonParams.TryGetValue("maxLevel", out string mlStr)
			&& int.TryParse(mlStr, out int ml) && ml > maxLevel)
		{
			maxLevel = ml;
		}

		string input = Microsoft.VisualBasic.Interaction.InputBox(
			$"批量添加等级描述行。\n\n当前技能有 {maxLevel} 个等级。\n请输入要添加到第几级 (从 h1 开始):\n\n已存在的行不会被覆盖。",
			"批量添加等级描述",
			maxLevel.ToString());
		if (string.IsNullOrWhiteSpace(input)) return;
		if (!int.TryParse(input, out int count) || count <= 0) return;

		// Collect existing keys
		var existing = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
		foreach (DataGridViewRow row in dgvHLevels.Rows)
		{
			if (row.IsNewRow) continue;
			string key = row.Cells["Key"].Value?.ToString()?.Trim() ?? "";
			if (!string.IsNullOrEmpty(key)) existing.Add(key);
		}

		int added = 0;
		for (int i = 1; i <= count; i++)
		{
			string key = $"h{i}";
			if (!existing.Contains(key))
			{
				dgvHLevels.Rows.Add(key, "");
				added++;
			}
		}
		Console.WriteLine($"[文本编辑] 批量添加: 共添加 {added} 行 (h1~h{count})，跳过 {count - added} 行已存在");
	}

	private void HLevelsMenu_DeleteRow(object sender, EventArgs e)
	{
		if (dgvHLevels == null) return;
		var selected = dgvHLevels.SelectedRows;
		if (selected.Count == 0 && dgvHLevels.CurrentRow != null && !dgvHLevels.CurrentRow.IsNewRow)
		{
			string key = dgvHLevels.CurrentRow.Cells["Key"]?.Value?.ToString() ?? "(空)";
			if (MessageBox.Show($"确认删除等级描述 \"{key}\" 吗？", "确认删除", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation) != DialogResult.Yes)
				return;
			dgvHLevels.Rows.Remove(dgvHLevels.CurrentRow);
			return;
		}
		var toRemove = new List<DataGridViewRow>();
		foreach (DataGridViewRow row in selected)
		{
			if (!row.IsNewRow) toRemove.Add(row);
		}
		if (toRemove.Count == 0) return;
		if (MessageBox.Show($"确认删除选中的 {toRemove.Count} 行等级描述吗？", "确认删除", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation) != DialogResult.Yes)
			return;
		foreach (var row in toRemove)
			dgvHLevels.Rows.Remove(row);
	}

	private void HLevelsMenu_ClearAll(object sender, EventArgs e)
	{
		if (dgvHLevels == null) return;
		if (dgvHLevels.Rows.Count <= 1) return; // only the new-row placeholder
		var result = MessageBox.Show("确定要清空所有等级描述吗？", "确认清空", MessageBoxButtons.YesNo, MessageBoxIcon.Warning);
		if (result == DialogResult.Yes)
			dgvHLevels.Rows.Clear();
	}

	private void HLevelsMenu_CopyFromTemplate(object sender, EventArgs e)
	{
		if (dgvHLevels == null || txtHTemplate == null) return;
		string template = txtHTemplate.Text?.Trim() ?? "";
		if (string.IsNullOrEmpty(template))
		{
			MessageBox.Show("通用 h 模板为空，无法复制。\n请先在「通用文本」标签页填写 h 模板。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
			return;
		}

		int count = 0;
		foreach (DataGridViewRow row in dgvHLevels.Rows)
		{
			if (row.IsNewRow) continue;
			string val = row.Cells["Value"].Value?.ToString() ?? "";
			if (string.IsNullOrWhiteSpace(val))
			{
				row.Cells["Value"].Value = template;
				count++;
			}
		}
		if (count > 0)
			Console.WriteLine($"[文本编辑] 从 h 模板复制到 {count} 个空行");
		else
			MessageBox.Show("没有空行可以填充。如果要覆盖，请先清空对应行。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
	}

	private static void ReindexEffectFrames(List<WzEffectFrame> frames)
	{
		if (frames == null)
		{
			return;
		}
		for (int i = 0; i < frames.Count; i++)
		{
			frames[i].Index = i;
		}
	}

	private void PopulateEffectFrames(List<WzEffectFrame> frames)
	{
		lvEffectFrames.Items.Clear();
		if (frames == null || frames.Count == 0)
		{
			_picEffectPreview.Image = null;
			return;
		}
		foreach (WzEffectFrame frame in frames)
		{
			ListViewItem listViewItem = new ListViewItem(frame.Index.ToString());
			listViewItem.SubItems.Add($"{frame.Width}x{frame.Height}");
			listViewItem.SubItems.Add(frame.Delay.ToString());
			listViewItem.SubItems.Add(BuildEffectFrameMetaSummary(frame));
			lvEffectFrames.Items.Add(listViewItem);
		}
	}

	private void TreeNode_DoubleClick(object sender, TreeNodeMouseClickEventArgs e)
	{
		if (e.Node.Tag is WzNodeInfo wzNodeInfo && (wzNodeInfo.Children == null || wzNodeInfo.Children.Count <= 0))
		{
			EditNodeValue(e.Node, wzNodeInfo);
		}
	}

	private void TreeMenu_EditValue(object sender, EventArgs e)
	{
		if (treeSkillData.SelectedNode != null && treeSkillData.SelectedNode.Tag is WzNodeInfo info)
		{
			EditNodeValue(treeSkillData.SelectedNode, info);
		}
	}

	private void EditNodeValue(TreeNode tn, WzNodeInfo info)
	{
		string text = ShowInputBox("编辑节点值", "节点: " + info.Name + "\n当前: " + info.Value, info.Value ?? "");
		if (text != null)
		{
			PushUndoSnapshot();
			info.Value = text;
			tn.Text = (string.IsNullOrEmpty(info.Value) ? ("[" + info.TypeName + "] " + info.Name) : $"[{info.TypeName}] {info.Name} = {info.Value}");
		}
	}

	private void TreeMenu_AddChild(object sender, EventArgs e)
	{
		if (treeSkillData.SelectedNode == null || !(treeSkillData.SelectedNode.Tag is WzNodeInfo wzNodeInfo))
		{
			return;
		}
		string text = ShowInputBox("添加子节点", "节点名称:", "");
		if (!string.IsNullOrEmpty(text))
		{
			string text2 = ShowInputBox("添加子节点", "节点值（留空表示容器）:", "");
			PushUndoSnapshot();
			if (wzNodeInfo.Children == null)
			{
				wzNodeInfo.Children = new List<WzNodeInfo>();
			}
			WzNodeInfo wzNodeInfo2 = new WzNodeInfo
			{
				Name = text,
				TypeName = "自定义",
				Value = (text2 ?? ""),
				Children = new List<WzNodeInfo>()
			};
			wzNodeInfo.Children.Add(wzNodeInfo2);
			treeSkillData.SelectedNode.Nodes.Add(BuildTreeNode(wzNodeInfo2));
			treeSkillData.SelectedNode.Expand();
		}
	}

	private void TreeMenu_DeleteNode(object sender, EventArgs e)
	{
		if (treeSkillData.SelectedNode == null || !(treeSkillData.SelectedNode.Tag is WzNodeInfo item))
		{
			return;
		}
		if (MessageBox.Show($"确认删除节点 \"{item.Name}\" 吗？", "确认删除", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation) != DialogResult.Yes)
		{
			return;
		}
		PushUndoSnapshot();
		TreeNode parent = treeSkillData.SelectedNode.Parent;
		if (parent != null)
		{
			if (parent.Tag is WzNodeInfo { Children: not null } wzNodeInfo)
			{
				wzNodeInfo.Children.Remove(item);
			}
			parent.Nodes.Remove(treeSkillData.SelectedNode);
		}
		else
		{
			treeSkillData.Nodes.Remove(treeSkillData.SelectedNode);
		}
	}

	private void TreeMenu_CopyNode(object sender, EventArgs e)
	{
		if (treeSkillData.SelectedNode != null && treeSkillData.SelectedNode.Tag is WzNodeInfo wzNodeInfo)
		{
			_clipboardNode = DeepCopyNode(wzNodeInfo);
			Console.WriteLine("[GUI] 已复制节点 " + wzNodeInfo.Name);
		}
	}

	private void TreeMenu_PasteNode(object sender, EventArgs e)
	{
		if (treeSkillData.SelectedNode != null && _clipboardNode != null && treeSkillData.SelectedNode.Tag is WzNodeInfo wzNodeInfo)
		{
			PushUndoSnapshot();
			if (wzNodeInfo.Children == null)
			{
				wzNodeInfo.Children = new List<WzNodeInfo>();
			}
			WzNodeInfo wzNodeInfo2 = DeepCopyNode(_clipboardNode);
			wzNodeInfo.Children.Add(wzNodeInfo2);
			treeSkillData.SelectedNode.Nodes.Add(BuildTreeNode(wzNodeInfo2));
			treeSkillData.SelectedNode.Expand();
			Console.WriteLine("[GUI] 已粘贴节点 " + wzNodeInfo2.Name + " -> " + wzNodeInfo.Name);
		}
	}

	private WzNodeInfo DeepCopyNode(WzNodeInfo src)
	{
		WzNodeInfo wzNodeInfo = new WzNodeInfo
		{
			Name = src.Name,
			TypeName = src.TypeName,
			Value = src.Value
		};
		if (src.Children != null)
		{
			wzNodeInfo.Children = new List<WzNodeInfo>();
			foreach (WzNodeInfo child in src.Children)
			{
				wzNodeInfo.Children.Add(DeepCopyNode(child));
			}
		}
		return wzNodeInfo;
	}

	private void LvEffectFrames_SelectedChanged(object sender, EventArgs e)
	{
		List<WzEffectFrame> activeEffectFrames = GetActiveEffectFrames(createIfMissing: false);
		if (lvEffectFrames.SelectedIndices.Count == 0 || activeEffectFrames == null)
		{
			_picEffectPreview.Image = null;
			return;
		}
		int num = lvEffectFrames.SelectedIndices[0];
		if (num >= 0 && num < activeEffectFrames.Count)
		{
			SafeSetImage(_picEffectPreview, activeEffectFrames[num].Bitmap);
		}
		else
		{
			_picEffectPreview.Image = null;
		}
	}

	private void FxMenu_CopyFrames(object sender, EventArgs e)
	{
		List<WzEffectFrame> activeEffectFrames = GetActiveEffectFrames(createIfMissing: false);
		if (lvEffectFrames.SelectedIndices.Count == 0 || activeEffectFrames == null)
		{
			return;
		}
		_clipboardFrames = new List<WzEffectFrame>();
		foreach (int selectedIndex in lvEffectFrames.SelectedIndices)
		{
			if (selectedIndex >= 0 && selectedIndex < activeEffectFrames.Count)
			{
				WzEffectFrame cloned = WzEffectFrame.CloneShallowBitmap(activeEffectFrames[selectedIndex]);
				if (cloned != null)
				{
					_clipboardFrames.Add(cloned);
				}
			}
		}
		Console.WriteLine($"[GUI] 已复制 {_clipboardFrames.Count} 帧");
	}

	private void FxMenu_PasteFrames(object sender, EventArgs e)
	{
		if (_clipboardFrames == null || _clipboardFrames.Count == 0)
		{
			return;
		}
		PushUndoSnapshot();
		List<WzEffectFrame> activeEffectFrames = GetActiveEffectFrames(createIfMissing: true);
		foreach (WzEffectFrame clipboardFrame in _clipboardFrames)
		{
			WzEffectFrame cloned = WzEffectFrame.CloneShallowBitmap(clipboardFrame);
			if (cloned != null)
			{
				cloned.Index = activeEffectFrames.Count;
				activeEffectFrames.Add(cloned);
			}
		}
		_hasManualEffectEdit = true;
		PopulateEffectFrames(activeEffectFrames);
		Console.WriteLine($"[GUI] 已粘贴 {_clipboardFrames.Count} 帧");
	}

	private void FxList_DragEnter(object sender, DragEventArgs e)
	{
		if (e.Data.GetDataPresent(DataFormats.FileDrop))
		{
			e.Effect = DragDropEffects.Copy;
		}
	}

	private void FxList_DragDrop(object sender, DragEventArgs e)
	{
		string[] array = (string[])e.Data.GetData(DataFormats.FileDrop);
		if (array == null || array.Length == 0)
		{
			return;
		}
		PushUndoSnapshot();
		List<WzEffectFrame> activeEffectFrames = GetActiveEffectFrames(createIfMissing: true);
		int num = 0;
		string[] array2 = array;
		foreach (string text in array2)
		{
			string text2 = Path.GetExtension(text).ToLower();
			if (!(text2 != ".png") || !(text2 != ".bmp") || !(text2 != ".gif") || !(text2 != ".jpg"))
			{
				try
				{
					Bitmap bitmap = new Bitmap(text);
					activeEffectFrames.Add(new WzEffectFrame
					{
						Index = activeEffectFrames.Count,
						Bitmap = bitmap,
						Width = bitmap.Width,
						Height = bitmap.Height,
						Delay = 100
					});
					num++;
				}
				catch
				{
				}
			}
		}
		if (num > 0)
		{
			_hasManualEffectEdit = true;
			PopulateEffectFrames(activeEffectFrames);
			Console.WriteLine($"[GUI] 拖拽导入 {num} 帧");
		}
	}

	private void DgvLevelParams_CellBeginEdit(object sender, DataGridViewCellCancelEventArgs e)
	{
		if (_isRestoringSnapshot || _dgvCellEditUndoCaptured)
		{
			return;
		}
		_dgvCellEditUndoCaptured = true;
		PushUndoSnapshot();
	}

	private void DgvLevelParams_CellEndEdit(object sender, DataGridViewCellEventArgs e)
	{
		_dgvCellEditUndoCaptured = false;
		if (_isRestoringSnapshot)
		{
			return;
		}
		SyncLevelParamsFromGrid();
	}

	private void DgvMenu_AddColumn(object sender, EventArgs e)
	{
		string text = ShowInputBox("添加参数列", "参数名:", "");
		if (!string.IsNullOrEmpty(text))
		{
			PushUndoSnapshot();
			dgvLevelParams.Columns.Add(text, text);
			SyncLevelParamsFromGrid();
		}
	}

	private void DgvMenu_DeleteColumn(object sender, EventArgs e)
	{
		if (dgvLevelParams.CurrentCell != null)
		{
			int columnIndex = dgvLevelParams.CurrentCell.ColumnIndex;
			if (columnIndex > 0)
			{
				string columnName = dgvLevelParams.Columns[columnIndex]?.Name ?? "(未命名)";
				if (MessageBox.Show($"确认删除参数列 \"{columnName}\" 吗？", "确认删除", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation) != DialogResult.Yes)
					return;
				PushUndoSnapshot();
				dgvLevelParams.Columns.RemoveAt(columnIndex);
				SyncLevelParamsFromGrid();
			}
		}
	}

	private void DgvMenu_AddRow(object sender, EventArgs e)
	{
		PushUndoSnapshot();
		int num = dgvLevelParams.Rows.Count + 1;
		List<string> list = new List<string> { num.ToString() };
		for (int i = 1; i < dgvLevelParams.Columns.Count; i++)
		{
			list.Add("");
		}
		DataGridViewRowCollection rows = dgvLevelParams.Rows;
		object[] values = list.ToArray();
		rows.Add(values);
		SyncLevelParamsFromGrid();
	}

	private void DgvMenu_DeleteRow(object sender, EventArgs e)
	{
		if (dgvLevelParams.CurrentRow != null)
		{
			string levelName = dgvLevelParams.CurrentRow.Cells[0]?.Value?.ToString() ?? "(空)";
			if (MessageBox.Show($"确认删除等级行 \"{levelName}\" 吗？", "确认删除", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation) != DialogResult.Yes)
				return;
			PushUndoSnapshot();
			dgvLevelParams.Rows.Remove(dgvLevelParams.CurrentRow);
			SyncLevelParamsFromGrid();
		}
	}

	private void NudMaxLevel_ValueChanged(object sender, EventArgs e)
	{
		if (_isRestoringSnapshot)
		{
			return;
		}
		SyncMaxLevelIntoCommonParams(createIfMissing: false);
	}

	private bool IsCommonParamsGridMode()
	{
		return dgvLevelParams != null
			&& dgvLevelParams.Columns.Count == 2
			&& string.Equals(dgvLevelParams.Columns[0].Name, "Param", StringComparison.OrdinalIgnoreCase);
	}

	private void SyncMaxLevelIntoCommonParams(bool createIfMissing)
	{
		if (nudMaxLevel == null)
		{
			return;
		}
		string text = ((int)nudMaxLevel.Value).ToString();
		if (_editState.EditedLevelParams == null)
		{
			_editState.EditedLevelParams = new Dictionary<int, Dictionary<string, string>>();
		}
		bool flag = _editState.EditedLevelParams.TryGetValue(0, out var value);
		if (!flag && createIfMissing && IsCommonParamsGridMode())
		{
			value = new Dictionary<string, string>();
			_editState.EditedLevelParams[0] = value;
			flag = true;
		}
		if (flag && value != null)
		{
			value["maxLevel"] = text;
		}
		if (!IsCommonParamsGridMode())
		{
			return;
		}
		DataGridViewRow dataGridViewRow = null;
		foreach (DataGridViewRow row in dgvLevelParams.Rows)
		{
			if (row == null || row.IsNewRow)
			{
				continue;
			}
			string text2 = row.Cells[0].Value?.ToString();
			if (string.Equals(text2, "maxLevel", StringComparison.OrdinalIgnoreCase))
			{
				dataGridViewRow = row;
				break;
			}
		}
		if (dataGridViewRow != null)
		{
			dataGridViewRow.Cells[1].Value = text;
		}
		else if (createIfMissing)
		{
			dgvLevelParams.Rows.Add("maxLevel", text);
		}
	}

	private void SyncLevelParamsFromGrid()
	{
		Dictionary<string, string> dictionary = null;
		if (_editState.EditedLevelParams != null && _editState.EditedLevelParams.TryGetValue(0, out var value) && value != null)
		{
			dictionary = new Dictionary<string, string>(value);
		}
		if (_editState.EditedLevelParams == null)
		{
			_editState.EditedLevelParams = new Dictionary<int, Dictionary<string, string>>();
		}
		else
		{
			_editState.EditedLevelParams.Clear();
		}
		if (dgvLevelParams.Columns.Count == 2 && dgvLevelParams.Columns[0].Name == "Param")
		{
			Dictionary<string, string> dictionary3 = new Dictionary<string, string>();
			for (int i = 0; i < dgvLevelParams.Rows.Count; i++)
			{
				DataGridViewRow dataGridViewRow = dgvLevelParams.Rows[i];
				string text = dataGridViewRow.Cells[0].Value?.ToString();
				string text2 = dataGridViewRow.Cells[1].Value?.ToString();
				if (!string.IsNullOrEmpty(text))
				{
					dictionary3[text] = text2 ?? "";
				}
			}
			_editState.EditedLevelParams[0] = dictionary3;
			SyncMaxLevelFromCommonParams(dictionary3);
			return;
		}
		for (int j = 0; j < dgvLevelParams.Rows.Count; j++)
		{
			DataGridViewRow dataGridViewRow2 = dgvLevelParams.Rows[j];
			if (!int.TryParse(dataGridViewRow2.Cells[0].Value?.ToString(), out var result))
			{
				continue;
			}
			Dictionary<string, string> dictionary2 = new Dictionary<string, string>();
			for (int k = 1; k < dgvLevelParams.Columns.Count; k++)
			{
				string cellValue = dataGridViewRow2.Cells[k].Value?.ToString();
				if (!string.IsNullOrEmpty(cellValue))
				{
					dictionary2[dgvLevelParams.Columns[k].Name] = cellValue;
				}
			}
			_editState.EditedLevelParams[result] = dictionary2;
		}
		if (dictionary != null && dictionary.Count > 0 && !_editState.EditedLevelParams.ContainsKey(0))
		{
			_editState.EditedLevelParams[0] = dictionary;
		}
	}

	private void BtnCopyEffects_Click(object sender, EventArgs e)
	{
		string text = ShowInputBox("复制特效", "输入源技能ID:", "");
		if (string.IsNullOrEmpty(text))
		{
			return;
		}
		if (!int.TryParse(text, out var result) || result <= 0)
		{
			MessageBox.Show("无效ID");
			return;
		}
		try
		{
			WzSkillData wzSkillData = _wzLoader.LoadSkill(result);
			Dictionary<string, List<WzEffectFrame>> dictionary = EditState.CloneEffectsByNode(wzSkillData.EffectFramesByNode);
			if ((dictionary == null || dictionary.Count == 0) && wzSkillData.EffectFrames != null && wzSkillData.EffectFrames.Count > 0)
			{
				dictionary = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase)
				{
					["effect"] = EditState.CloneEffectFrameList(wzSkillData.EffectFrames)
				};
			}
			if (dictionary != null && dictionary.Count > 0)
			{
				PushUndoSnapshot();
				_editState.EditedEffectsByNode = dictionary;
				_hasManualEffectEdit = true;
				// Also copy per-level animation frames if present
				if (wzSkillData.LevelAnimFramesByNode != null && wzSkillData.LevelAnimFramesByNode.Count > 0)
				{
					_editState.EditedLevelAnimFramesByNode = EditState.CloneLevelAnimFramesByNode(wzSkillData.LevelAnimFramesByNode);
				}
				RefreshEffectNodeSelector("effect", createIfMissing: true);
				PopulateEffectFrames(_editState.EditedEffects);
				RefreshAnimLevelSelector(preserveSelection: false);
				int num = 0;
				foreach (KeyValuePair<string, List<WzEffectFrame>> item in dictionary)
				{
					num += item.Value?.Count ?? 0;
				}
				int levelAnimCount = _editState.EditedLevelAnimFramesByNode?.Count ?? 0;
				Console.WriteLine($"[GUI] 已复制 {num} 帧特效（{dictionary.Count} 个节点, {levelAnimCount} 个等级动画），来源 skill/{result}");
			}
			else
			{
				MessageBox.Show("该技能没有特效帧");
			}
		}
		catch (Exception ex)
		{
			MessageBox.Show("加载失败: " + ex.Message);
		}
	}

	private void FxMenu_AddFrame(object sender, EventArgs e)
	{
		OpenFileDialog openFileDialog = new OpenFileDialog
		{
			Filter = "图片|*.png;*.bmp|全部文件|*.*"
		};
		try
		{
			if (openFileDialog.ShowDialog() != DialogResult.OK)
			{
				return;
			}
			try
			{
				Bitmap bitmap = new Bitmap(openFileDialog.FileName);
				string text = ShowInputBox("添加帧", "延迟(ms):", "100");
				int result = 100;
				if (!string.IsNullOrEmpty(text))
				{
					int.TryParse(text, out result);
				}
				PushUndoSnapshot();
				List<WzEffectFrame> activeEffectFrames = GetActiveEffectFrames(createIfMissing: true);
				activeEffectFrames.Add(new WzEffectFrame
				{
					Index = activeEffectFrames.Count,
					Bitmap = bitmap,
					Width = bitmap.Width,
					Height = bitmap.Height,
					Delay = result
				});
				_hasManualEffectEdit = true;
				PopulateEffectFrames(activeEffectFrames);
			}
			catch (Exception ex)
			{
				MessageBox.Show("加载失败: " + ex.Message);
			}
		}
		finally
		{
			((IDisposable)(object)openFileDialog)?.Dispose();
		}
	}

	private void FxMenu_ReplaceFrame(object sender, EventArgs e)
	{
		List<WzEffectFrame> activeEffectFrames = GetActiveEffectFrames(createIfMissing: false);
		if (lvEffectFrames.SelectedIndices.Count == 0 || activeEffectFrames == null)
		{
			return;
		}
		int num = lvEffectFrames.SelectedIndices[0];
		if (num >= activeEffectFrames.Count)
		{
			return;
		}
		OpenFileDialog openFileDialog = new OpenFileDialog
		{
			Filter = "图片|*.png;*.bmp|全部文件|*.*"
		};
		try
		{
			if (openFileDialog.ShowDialog() == DialogResult.OK)
			{
				PushUndoSnapshot();
				Bitmap bitmap = new Bitmap(openFileDialog.FileName);
				activeEffectFrames[num].Bitmap = bitmap;
				activeEffectFrames[num].Width = bitmap.Width;
				activeEffectFrames[num].Height = bitmap.Height;
				_hasManualEffectEdit = true;
				PopulateEffectFrames(activeEffectFrames);
			}
		}
		finally
		{
			((IDisposable)(object)openFileDialog)?.Dispose();
		}
	}

	private void FxMenu_EditDelay(object sender, EventArgs e)
	{
		List<WzEffectFrame> activeEffectFrames = GetActiveEffectFrames(createIfMissing: false);
		if (lvEffectFrames.SelectedIndices.Count == 0 || activeEffectFrames == null)
		{
			return;
		}
		int num = lvEffectFrames.SelectedIndices[0];
		if (num >= activeEffectFrames.Count)
		{
			return;
		}
		string text = ShowInputBox("编辑延迟", "延迟(ms):", activeEffectFrames[num].Delay.ToString());
		if (text != null)
		{
			if (int.TryParse(text, out var result))
			{
				PushUndoSnapshot();
				activeEffectFrames[num].Delay = result;
				_hasManualEffectEdit = true;
			}
			PopulateEffectFrames(activeEffectFrames);
		}
	}

	private void SyncMaxLevelFromCommonParams(Dictionary<string, string> commonParams)
	{
		if (_isRestoringSnapshot || nudMaxLevel == null || commonParams == null)
		{
			return;
		}
		string text = null;
		foreach (KeyValuePair<string, string> commonParam in commonParams)
		{
			if (string.Equals(commonParam.Key, "maxLevel", StringComparison.OrdinalIgnoreCase))
			{
				text = commonParam.Value;
				break;
			}
		}
		if (string.IsNullOrWhiteSpace(text) || !int.TryParse(text.Trim(), out var result))
		{
			return;
		}
		int value = Math.Min((int)nudMaxLevel.Maximum, Math.Max((int)nudMaxLevel.Minimum, result));
		if ((int)nudMaxLevel.Value != value)
		{
			nudMaxLevel.Value = value;
		}
	}

	private void FxMenu_EditFrameMeta(object sender, EventArgs e)
	{
		if (!TryGetSelectedEffectFrame(out var frame, out var index))
		{
			return;
		}

		string initialJson = BuildEffectFrameMetaJson(frame);
		string edited = ShowMultiLineInputBox("编辑帧定位/参数", "JSON格式：vectors里填x/y，props里填z等标量", initialJson);
		if (edited == null)
		{
			return;
		}

		if (!TryParseEffectFrameMetaJson(edited, out var vectors, out var frameProps, out var err))
		{
			MessageBox.Show("格式错误: " + err, "编辑失败");
			return;
		}

		PushUndoSnapshot();
		frame.Vectors = vectors;
		frame.FrameProps = frameProps;
		_hasManualEffectEdit = true;
		PopulateEffectFrames(GetActiveEffectFrames(createIfMissing: false));
		if (index >= 0 && index < lvEffectFrames.Items.Count)
		{
			lvEffectFrames.Items[index].Selected = true;
			lvEffectFrames.Items[index].Focused = true;
		}
	}

	private bool TryGetSelectedEffectFrame(out WzEffectFrame frame, out int index)
	{
		frame = null;
		index = -1;
		List<WzEffectFrame> activeEffectFrames = GetActiveEffectFrames(createIfMissing: false);
		if (lvEffectFrames.SelectedIndices.Count == 0 || activeEffectFrames == null)
		{
			return false;
		}
		index = lvEffectFrames.SelectedIndices[0];
		if (index < 0 || index >= activeEffectFrames.Count)
		{
			return false;
		}
		frame = activeEffectFrames[index];
		return frame != null;
	}

	private string BuildEffectFrameMetaSummary(WzEffectFrame frame)
	{
		if (frame == null)
		{
			return "";
		}

		List<string> tokens = new List<string>();
		string[] preferred = new string[5] { "origin", "head", "vector", "border", "crosshair" };
		HashSet<string> used = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

		if (frame.Vectors != null)
		{
			foreach (string key in preferred)
			{
				if (TryGetVectorByName(frame.Vectors, key, out var vec))
				{
					tokens.Add(key + "=" + vec.X + "," + vec.Y);
					used.Add(key);
				}
			}
			foreach (KeyValuePair<string, WzFrameVector> vector in frame.Vectors)
			{
				if (string.IsNullOrEmpty(vector.Key) || vector.Value == null || used.Contains(vector.Key))
				{
					continue;
				}
				tokens.Add(vector.Key + "=" + vector.Value.X + "," + vector.Value.Y);
			}
		}

		if (frame.FrameProps != null)
		{
			List<string> list = new List<string>(frame.FrameProps.Keys);
			list.Sort(StringComparer.OrdinalIgnoreCase);
			foreach (string item2 in list)
			{
				if (string.IsNullOrEmpty(item2))
				{
					continue;
				}
				frame.FrameProps.TryGetValue(item2, out var value);
				tokens.Add(item2 + "=" + (value ?? ""));
			}
		}

		string text = string.Join("; ", tokens);
		if (text.Length > 120)
		{
			text = text.Substring(0, 117) + "...";
		}
		return text;
	}

	private static bool TryGetVectorByName(Dictionary<string, WzFrameVector> vectors, string key, out WzFrameVector vector)
	{
		vector = null;
		if (vectors == null || string.IsNullOrEmpty(key))
		{
			return false;
		}
		foreach (KeyValuePair<string, WzFrameVector> item in vectors)
		{
			if (item.Key != null && string.Equals(item.Key, key, StringComparison.OrdinalIgnoreCase))
			{
				vector = item.Value;
				return vector != null;
			}
		}
		return false;
	}

	private string BuildEffectFrameMetaJson(WzEffectFrame frame)
	{
		Dictionary<string, object> root = new Dictionary<string, object>();
		Dictionary<string, object> vectors = new Dictionary<string, object>();
		Dictionary<string, object> props = new Dictionary<string, object>();

		if (frame?.Vectors != null && frame.Vectors.Count > 0)
		{
			string[] preferred = new string[5] { "origin", "head", "vector", "border", "crosshair" };
			HashSet<string> used = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

			foreach (string key in preferred)
			{
				if (TryGetVectorByName(frame.Vectors, key, out var vec))
				{
					Dictionary<string, object> item = new Dictionary<string, object>();
					item["x"] = (long)vec.X;
					item["y"] = (long)vec.Y;
					vectors[key] = item;
					used.Add(key);
				}
			}

			foreach (KeyValuePair<string, WzFrameVector> vector in frame.Vectors)
			{
				if (string.IsNullOrEmpty(vector.Key) || vector.Value == null || used.Contains(vector.Key))
				{
					continue;
				}
				Dictionary<string, object> item2 = new Dictionary<string, object>();
				item2["x"] = (long)vector.Value.X;
				item2["y"] = (long)vector.Value.Y;
				vectors[vector.Key] = item2;
			}
		}

		if (frame?.FrameProps != null && frame.FrameProps.Count > 0)
		{
			List<string> list = new List<string>(frame.FrameProps.Keys);
			list.Sort(StringComparer.OrdinalIgnoreCase);
			foreach (string item3 in list)
			{
				if (string.IsNullOrEmpty(item3))
				{
					continue;
				}
				frame.FrameProps.TryGetValue(item3, out var value2);
				props[item3] = value2 ?? "";
			}
		}

		root["vectors"] = vectors;
		root["props"] = props;
		return SimpleJson.Serialize(root, 2);
	}

	private static bool TryParseEffectFrameMetaJson(
		string json,
		out Dictionary<string, WzFrameVector> vectors,
		out Dictionary<string, string> frameProps,
		out string error)
	{
		vectors = new Dictionary<string, WzFrameVector>(StringComparer.OrdinalIgnoreCase);
		frameProps = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
		error = "";

		try
		{
			Dictionary<string, object> root = SimpleJson.ParseObject(json ?? "{}");
			Dictionary<string, object> vectorObj = SimpleJson.GetObject(root, "vectors") ?? new Dictionary<string, object>();
			foreach (KeyValuePair<string, object> item in vectorObj)
			{
				if (string.IsNullOrEmpty(item.Key))
				{
					continue;
				}
				if (!(item.Value is Dictionary<string, object> dictionary))
				{
					continue;
				}
				int num = SimpleJson.GetInt(dictionary, "x", 0);
				int num2 = SimpleJson.GetInt(dictionary, "y", 0);
				vectors[item.Key] = new WzFrameVector(num, num2);
			}

			Dictionary<string, object> propObj = SimpleJson.GetObject(root, "props") ?? new Dictionary<string, object>();
			foreach (KeyValuePair<string, object> item2 in propObj)
			{
				if (!string.IsNullOrEmpty(item2.Key) && item2.Value != null)
				{
					frameProps[item2.Key] = item2.Value.ToString();
				}
			}
			return true;
		}
		catch (Exception ex)
		{
			error = ex.Message;
			return false;
		}
	}

	private void FxMenu_DeleteFrame(object sender, EventArgs e)
	{
		List<WzEffectFrame> activeEffectFrames = GetActiveEffectFrames(createIfMissing: false);
		if (lvEffectFrames.SelectedIndices.Count == 0 || activeEffectFrames == null)
		{
			return;
		}

		List<int> indexes = new List<int>();
		foreach (int idx in lvEffectFrames.SelectedIndices)
		{
			if (idx >= 0 && idx < activeEffectFrames.Count)
				indexes.Add(idx);
		}
		if (indexes.Count == 0)
			return;
		if (MessageBox.Show($"确认删除选中的 {indexes.Count} 帧吗？", "确认删除", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation) != DialogResult.Yes)
			return;

		PushUndoSnapshot();
		indexes.Sort();
		for (int i = indexes.Count - 1; i >= 0; i--)
			activeEffectFrames.RemoveAt(indexes[i]);

		ReindexEffectFrames(activeEffectFrames);
		_hasManualEffectEdit = true;
		PopulateEffectFrames(activeEffectFrames);
		Console.WriteLine($"[GUI] 已删除 {indexes.Count} 帧");

		int next = Math.Min(indexes[0], activeEffectFrames.Count - 1);
		if (next >= 0 && next < lvEffectFrames.Items.Count)
		{
			lvEffectFrames.Items[next].Selected = true;
			lvEffectFrames.Items[next].Focused = true;
		}
	}

	private static bool StringDictEquals(Dictionary<string, string> left, Dictionary<string, string> right)
	{
		if (ReferenceEquals(left, right))
		{
			return true;
		}
		if (left == null || right == null || left.Count != right.Count)
		{
			return false;
		}
		foreach (KeyValuePair<string, string> item in left)
		{
			if (!right.TryGetValue(item.Key, out string value) || !string.Equals(item.Value ?? "", value ?? "", StringComparison.Ordinal))
			{
				return false;
			}
		}
		return true;
	}

	private static bool LevelDictEquals(Dictionary<int, Dictionary<string, string>> left, Dictionary<int, Dictionary<string, string>> right)
	{
		if (ReferenceEquals(left, right))
		{
			return true;
		}
		if (left == null || right == null || left.Count != right.Count)
		{
			return false;
		}
		foreach (KeyValuePair<int, Dictionary<string, string>> item in left)
		{
			if (!right.TryGetValue(item.Key, out Dictionary<string, string> value) || !StringDictEquals(item.Value, value))
			{
				return false;
			}
		}
		return true;
	}

	private SkillDefinition BuildSkillFromForm()
	{
		if (!int.TryParse(txtSkillId.Text.Trim(), out var result) || result <= 0)
		{
			throw new Exception("技能ID必须为正整数");
		}
		if (dgvLevelParams != null && dgvLevelParams.IsCurrentCellInEditMode)
		{
			dgvLevelParams.EndEdit();
		}
		bool flag = false;
		if (dgvLevelParams != null && dgvLevelParams.Columns.Count == 2 && dgvLevelParams.Columns[0].Name == "Param")
		{
			flag = true;
		}
		if (!flag && _editState.EditedLevelParams != null && _editState.EditedLevelParams.TryGetValue(0, out var existingCommon) && existingCommon != null)
		{
			flag = true;
		}
		SyncMaxLevelIntoCommonParams(createIfMissing: flag);
		SyncLevelParamsFromGrid();
		SkillDefinition skillDefinition = new SkillDefinition();
		skillDefinition.SkillId = result;
		SkillDefinition skillDefinition2 = null;
		if (_editState.LoadedData != null)
		{
			WzSkillData loadedData = _editState.LoadedData;
			skillDefinition.Action = loadedData.Action ?? "";
			skillDefinition.InfoType = loadedData.InfoType;
			skillDefinition.PDesc = loadedData.PDesc ?? "";
			skillDefinition.Ph = loadedData.Ph ?? "";
			if (loadedData.SkillId > 0 && loadedData.SkillId != skillDefinition.SkillId)
			{
				skillDefinition.CloneFromSkillId = loadedData.SkillId;
			}
			if (loadedData.CommonParams != null)
			{
				foreach (KeyValuePair<string, string> commonParam in loadedData.CommonParams)
				{
					skillDefinition.Common[commonParam.Key] = commonParam.Value;
				}
			}
			if (loadedData.LevelParams != null && loadedData.LevelParams.Count > 0)
			{
				skillDefinition.Levels = new Dictionary<int, Dictionary<string, string>>();
				foreach (KeyValuePair<int, Dictionary<string, string>> levelParam in loadedData.LevelParams)
				{
					skillDefinition.Levels[levelParam.Key] = new Dictionary<string, string>(levelParam.Value);
				}
			}
			if (loadedData.HLevels != null)
			{
				foreach (KeyValuePair<string, string> hLevel in loadedData.HLevels)
				{
					skillDefinition.HLevels[hLevel.Key] = hLevel.Value;
				}
			}
		}
		if (_editState.EditingListIndex.HasValue)
		{
			int value = _editState.EditingListIndex.Value;
			if (value >= 0 && value < _pendingSkills.Count)
			{
				skillDefinition2 = _pendingSkills[value];
				if (string.IsNullOrEmpty(skillDefinition.Action))
				{
					skillDefinition.Action = skillDefinition2.Action ?? "";
				}
				if (skillDefinition.InfoType <= 1 && skillDefinition2.InfoType > 1)
				{
					skillDefinition.InfoType = skillDefinition2.InfoType;
				}
				if (skillDefinition.HLevels.Count == 0 && skillDefinition2.HLevels != null)
				{
					foreach (KeyValuePair<string, string> hLevel2 in skillDefinition2.HLevels)
					{
						skillDefinition.HLevels[hLevel2.Key] = hLevel2.Value;
					}
				}
				if (string.IsNullOrEmpty(skillDefinition.PDesc))
				{
					skillDefinition.PDesc = skillDefinition2.PDesc ?? "";
				}
				if (string.IsNullOrEmpty(skillDefinition.Ph))
				{
					skillDefinition.Ph = skillDefinition2.Ph ?? "";
				}
				if (skillDefinition.Levels == null && skillDefinition2.Levels != null && skillDefinition2.Levels.Count > 0)
				{
					skillDefinition.Levels = new Dictionary<int, Dictionary<string, string>>();
					foreach (KeyValuePair<int, Dictionary<string, string>> level in skillDefinition2.Levels)
					{
						skillDefinition.Levels[level.Key] = new Dictionary<string, string>(level.Value);
					}
				}
				if (skillDefinition.Common.Count == 0 && skillDefinition2.Common != null)
				{
					foreach (KeyValuePair<string, string> item in skillDefinition2.Common)
					{
						skillDefinition.Common[item.Key] = item.Value;
					}
				}
			}
		}
		skillDefinition.Name = txtName.Text.Trim();
		skillDefinition.Desc = txtDesc.Text.Trim();
		skillDefinition.Tab = (cboTab.SelectedItem as string) ?? "active";
		skillDefinition.MaxLevel = (int)nudMaxLevel.Value;
		skillDefinition.SuperSpCost = (int)nudSuperSpCost.Value;
		if (grpRoute.Enabled)
		{
			int selectedIndex = cboPacketRoute.SelectedIndex;
			skillDefinition.ReleaseType = ((selectedIndex >= 0 && selectedIndex < RouteNames.Length) ? RouteNames[selectedIndex] : "");
			if (int.TryParse(txtProxySkillId.Text.Trim(), out var result2))
			{
				skillDefinition.ProxySkillId = result2;
			}
			if (int.TryParse(txtVisualSkillId.Text.Trim(), out var result3))
			{
				skillDefinition.VisualSkillId = result3;
			}
		}
		if (int.TryParse((txtMountItemId?.Text ?? "").Trim(), out var result4) && result4 > 0)
		{
			skillDefinition.MountItemId = result4;
		}
		skillDefinition.MountResourceMode = GetSelectedMountResourceMode();
		if (int.TryParse((txtMountSourceItemId?.Text ?? "").Trim(), out var result5) && result5 > 0)
		{
			skillDefinition.MountSourceItemId = result5;
		}

		if (skillDefinition.MountItemId > 0)
		{
			int resolvedTamingMobId = 0;
			if (_mountEditorData != null
				&& _mountEditorData.MountItemId == skillDefinition.MountItemId
				&& _mountEditorData.TamingMobId > 0)
			{
				resolvedTamingMobId = _mountEditorData.TamingMobId;
			}
			else if (MountEditorService.TryReadActionTamingMobIdByMountItem(skillDefinition.MountItemId, out int fromAction))
			{
				resolvedTamingMobId = fromAction;
			}
			else if (int.TryParse((txtMountTamingMobId?.Text ?? "").Trim(), out int fromLegacy) && fromLegacy > 0)
			{
				resolvedTamingMobId = fromLegacy;
			}
			if (resolvedTamingMobId <= 0)
			{
				resolvedTamingMobId = MountEditorService.FindNextAvailableTamingMobId(Math.Abs(skillDefinition.MountItemId % 10000));
			}

			skillDefinition.MountTamingMobId = resolvedTamingMobId;
			if (txtMountTamingMobId != null)
				txtMountTamingMobId.Text = resolvedTamingMobId > 0 ? resolvedTamingMobId.ToString() : "";
		}

		if ((txtMountSpeed?.Visible ?? false) && TryParseOptionalInt(txtMountSpeed?.Text, out var mountSpeed))
		{
			skillDefinition.MountSpeedOverride = mountSpeed;
		}
		if ((txtMountJump?.Visible ?? false) && TryParseOptionalInt(txtMountJump?.Text, out var mountJump))
		{
			skillDefinition.MountJumpOverride = mountJump;
		}
		if ((txtMountFatigue?.Visible ?? false) && TryParseOptionalInt(txtMountFatigue?.Text, out var mountFatigue))
		{
			skillDefinition.MountFatigueOverride = mountFatigue;
		}
		if (skillDefinition.MountItemId <= 0)
		{
			skillDefinition.MountResourceMode = "config_only";
			skillDefinition.MountSourceItemId = 0;
			skillDefinition.MountTamingMobId = 0;
			skillDefinition.MountSpeedOverride = null;
			skillDefinition.MountJumpOverride = null;
			skillDefinition.MountFatigueOverride = null;
		}
		string text = cboReleaseClass.SelectedItem as string;
		skillDefinition.ReleaseClass = (!string.IsNullOrEmpty(text) ? text : ReleaseClassNames[0]);
		skillDefinition.BorrowDonorVisual = chkBorrowDonorVisual.Checked;
		skillDefinition.HideFromNativeSkillWnd = chkHideFromNative.Checked;
		skillDefinition.ShowInNativeWhenLearned = chkShowInNativeWhenLearned.Checked;
		skillDefinition.ShowInSuperWhenLearned = chkShowInSuperWhenLearned.Checked;
		skillDefinition.AllowNativeUpgradeFallback = chkAllowNativeFallback.Checked;
		skillDefinition.InjectToNative = chkInjectToNative.Checked;
		skillDefinition.AllowMountedFlight = chkAllowMountedFlight?.Checked ?? false;
		if (skillDefinition2 != null)
		{
			skillDefinition.InjectEnabled = skillDefinition2.InjectEnabled;
			skillDefinition.DonorSkillId = skillDefinition2.DonorSkillId;
			if (skillDefinition.MountItemId <= 0)
			{
				skillDefinition.MountItemId = skillDefinition2.MountItemId;
			}
			if (string.IsNullOrWhiteSpace(skillDefinition.MountResourceMode))
			{
				skillDefinition.MountResourceMode = skillDefinition2.MountResourceMode;
			}
			if (skillDefinition.MountSourceItemId <= 0)
			{
				skillDefinition.MountSourceItemId = skillDefinition2.MountSourceItemId;
			}
			if (skillDefinition.MountTamingMobId <= 0)
			{
				skillDefinition.MountTamingMobId = skillDefinition2.MountTamingMobId;
			}
			if (!skillDefinition.MountSpeedOverride.HasValue)
			{
				skillDefinition.MountSpeedOverride = skillDefinition2.MountSpeedOverride;
			}
			if (!skillDefinition.MountJumpOverride.HasValue)
			{
				skillDefinition.MountJumpOverride = skillDefinition2.MountJumpOverride;
			}
			if (!skillDefinition.MountFatigueOverride.HasValue)
			{
				skillDefinition.MountFatigueOverride = skillDefinition2.MountFatigueOverride;
			}
			skillDefinition.SuperSpCarrierSkillId = skillDefinition2.SuperSpCarrierSkillId;
			skillDefinition.ServerEnabled = skillDefinition2.ServerEnabled;
			if (skillDefinition.CloneFromSkillId <= 0 && skillDefinition2.CloneFromSkillId > 0)
			{
				skillDefinition.CloneFromSkillId = skillDefinition2.CloneFromSkillId;
			}
		}
		// Always persist effective icons (override > loaded data).
		// Otherwise "only change skillId then add to list" can accidentally drop icon fields.
		string effectiveIconBase64 = _editState.GetEffectiveIconBase64();
		string effectiveIconMOBase64 = _editState.GetEffectiveIconMOBase64();
		string effectiveIconDisBase64 = _editState.GetEffectiveIconDisBase64();
		if (!string.IsNullOrEmpty(effectiveIconBase64))
		{
			skillDefinition.IconBase64 = effectiveIconBase64;
		}
		if (!string.IsNullOrEmpty(effectiveIconMOBase64))
		{
			skillDefinition.IconMouseOverBase64 = effectiveIconMOBase64;
		}
		if (!string.IsNullOrEmpty(effectiveIconDisBase64))
		{
			skillDefinition.IconDisabledBase64 = effectiveIconDisBase64;
		}
		string text2 = InferType(skillDefinition.ReleaseType, skillDefinition.Tab);
		if (skillDefinition2 != null && string.Equals(skillDefinition2.Type, "newbie_level", StringComparison.OrdinalIgnoreCase))
		{
			skillDefinition.Type = "newbie_level";
		}
		else if (skillDefinition.Tab != "passive" && skillDefinition.Levels != null && skillDefinition.Levels.Count > 0)
		{
			skillDefinition.Type = "newbie_level";
		}
		else
		{
			skillDefinition.Type = text2;
		}
		bool flag2 = _editState.LoadedData != null || skillDefinition.CloneFromSkillId > 0;
		if (!flag2)
		{
			SkillTemplate tpl = SkillTemplate.Get(skillDefinition.Type);
			skillDefinition.ApplyTemplate(tpl);
		}
		if (_editState.EditedLevelParams != null && _editState.EditedLevelParams.Count > 0)
		{
			if (_editState.EditedLevelParams.TryGetValue(0, out var value2))
			{
				foreach (KeyValuePair<string, string> item2 in value2)
				{
					skillDefinition.Common[item2.Key] = item2.Value;
				}
			}
			bool flag3 = false;
			foreach (int key in _editState.EditedLevelParams.Keys)
			{
				if (key >= 1)
				{
					flag3 = true;
					break;
				}
			}
			if (flag3)
			{
				if (skillDefinition.Levels == null)
				{
					skillDefinition.Levels = new Dictionary<int, Dictionary<string, string>>();
				}
				foreach (KeyValuePair<int, Dictionary<string, string>> editedLevelParam in _editState.EditedLevelParams)
				{
					if (editedLevelParam.Key >= 1)
					{
						skillDefinition.Levels[editedLevelParam.Key] = new Dictionary<string, string>(editedLevelParam.Value);
					}
				}
			}
		}
		if (skillDefinition.Type != "passive" && skillDefinition.Levels != null && skillDefinition.Levels.Count > 0)
		{
			skillDefinition.Type = "newbie_level";
		}
		bool hasQueuedEffectOverride = skillDefinition2 != null && skillDefinition2.HasManualEffectOverride;
		bool shouldPersistEffectOverrides = _hasManualEffectEdit || hasQueuedEffectOverride;
		skillDefinition.HasManualEffectOverride = shouldPersistEffectOverrides;
		if (shouldPersistEffectOverrides && _editState.EditedEffectsByNode != null && _editState.EditedEffectsByNode.Count > 0)
		{
			skillDefinition.CachedEffectsByNode = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase);
			foreach (KeyValuePair<string, List<WzEffectFrame>> item3 in _editState.EditedEffectsByNode)
			{
				if (string.IsNullOrWhiteSpace(item3.Key) || item3.Value == null || item3.Value.Count == 0)
				{
					continue;
				}
				List<WzEffectFrame> list = new List<WzEffectFrame>();
				foreach (WzEffectFrame item4 in item3.Value)
				{
					WzEffectFrame wzEffectFrame = WzEffectFrame.CloneShallowBitmap(item4);
					if (wzEffectFrame != null)
					{
						list.Add(wzEffectFrame);
					}
				}
				if (list.Count > 0)
				{
					skillDefinition.CachedEffectsByNode[item3.Key] = list;
				}
			}
		}
		if (skillDefinition.CachedEffectsByNode != null && skillDefinition.CachedEffectsByNode.Count > 0)
		{
			if (!skillDefinition.CachedEffectsByNode.TryGetValue("effect", out var value3) || value3 == null || value3.Count == 0)
			{
				foreach (KeyValuePair<string, List<WzEffectFrame>> item5 in skillDefinition.CachedEffectsByNode)
				{
					if (item5.Value != null && item5.Value.Count > 0)
					{
						value3 = item5.Value;
						break;
					}
				}
			}
			if (value3 != null && value3.Count > 0)
			{
				skillDefinition.CachedEffects = new List<WzEffectFrame>();
				foreach (WzEffectFrame item6 in value3)
				{
					WzEffectFrame wzEffectFrame2 = WzEffectFrame.CloneShallowBitmap(item6);
					if (wzEffectFrame2 != null)
					{
						skillDefinition.CachedEffects.Add(wzEffectFrame2);
					}
				}
			}
		}
		else if (shouldPersistEffectOverrides && _editState.EditedEffects != null && _editState.EditedEffects.Count > 0)
		{
			skillDefinition.CachedEffects = new List<WzEffectFrame>();
			foreach (WzEffectFrame editedEffect in _editState.EditedEffects)
			{
				WzEffectFrame cloned = WzEffectFrame.CloneShallowBitmap(editedEffect);
				if (cloned != null)
				{
					skillDefinition.CachedEffects.Add(cloned);
				}
			}
		}
		if (_editState.EditedTree != null)
		{
			skillDefinition.CachedTree = _editState.EditedTree;
		}
		if (skillDefinition.Common != null && skillDefinition.Common.Count > 0)
		{
			skillDefinition.Common["maxLevel"] = skillDefinition.MaxLevel.ToString();
		}
		// Collect text fields from UI
		CollectTextFieldsIntoEditState();
		if (!string.IsNullOrWhiteSpace(_editState.EditedH))
			skillDefinition.H = _editState.EditedH;
		if (!string.IsNullOrWhiteSpace(_editState.EditedPDesc))
			skillDefinition.PDesc = _editState.EditedPDesc;
		if (!string.IsNullOrWhiteSpace(_editState.EditedPh))
			skillDefinition.Ph = _editState.EditedPh;
		// Collect h-levels from grid
		var gridHLevels = CollectHLevelsFromGrid();
		if (gridHLevels.Count > 0)
		{
			skillDefinition.HLevels = gridHLevels;
		}
		// Collect per-level animation frames (always persist if present — not gated by manual edit flag)
		if (_editState.EditedLevelAnimFramesByNode != null && _editState.EditedLevelAnimFramesByNode.Count > 0)
		{
			skillDefinition.LevelAnimFramesByNode = EditState.CloneLevelAnimFramesByNode(_editState.EditedLevelAnimFramesByNode);
		}
		bool isCloneSkill = skillDefinition.CloneFromSkillId > 0 && skillDefinition.CloneFromSkillId != skillDefinition.SkillId;
		if (isCloneSkill)
		{
			bool preserveCloneNode = skillDefinition2?.PreserveClonedNode ?? true;
			if (skillDefinition2 != null)
			{
				bool touchedStructurally = _hasManualEffectEdit
					|| !string.Equals(skillDefinition.Action ?? "", skillDefinition2.Action ?? "", StringComparison.Ordinal)
					|| skillDefinition.InfoType != skillDefinition2.InfoType
					|| skillDefinition.MaxLevel != skillDefinition2.MaxLevel
					|| !string.Equals(skillDefinition.IconBase64 ?? "", skillDefinition2.IconBase64 ?? "", StringComparison.Ordinal)
					|| !string.Equals(skillDefinition.IconMouseOverBase64 ?? "", skillDefinition2.IconMouseOverBase64 ?? "", StringComparison.Ordinal)
					|| !string.Equals(skillDefinition.IconDisabledBase64 ?? "", skillDefinition2.IconDisabledBase64 ?? "", StringComparison.Ordinal)
					|| !StringDictEquals(skillDefinition.Common, skillDefinition2.Common)
					|| !LevelDictEquals(skillDefinition.Levels, skillDefinition2.Levels);
				if (touchedStructurally)
				{
					preserveCloneNode = false;
				}
			}
			skillDefinition.PreserveClonedNode = preserveCloneNode;
		}
		else
		{
			skillDefinition.PreserveClonedNode = false;
		}
		skillDefinition.NormalizeTextFields();
		return skillDefinition;
	}

	private bool TryAutoCommitEditingSkill()
	{
		if (!_editState.EditingListIndex.HasValue)
		{
			return true;
		}
		int value = _editState.EditingListIndex.Value;
		if (value < 0 || value >= _pendingSkills.Count)
		{
			return true;
		}
		try
		{
			SkillDefinition skillDefinition = BuildSkillFromForm();
			SkillDefinition oldSkill = _pendingSkills[value];
			if (IsCarrierSkillId(skillDefinition.SkillId))
			{
				MessageBox.Show($"技能ID {skillDefinition.SkillId} 是载体技能ID，请在“设置”页维护，不加入列表。", "提示");
				return false;
			}
			for (int i = 0; i < _pendingSkills.Count; i++)
			{
				if (i != value && _pendingSkills[i].SkillId == skillDefinition.SkillId)
				{
					MessageBox.Show($"技能ID {skillDefinition.SkillId} 已在列表中存在", "重复");
					return false;
				}
			}
			UpdateSourceLabel(skillDefinition);
			QueueOldSkillWhenIdChanged(oldSkill, skillDefinition);
			RemoveDeletedQueueBySkillId(skillDefinition.SkillId);
			_pendingSkills[value] = skillDefinition;
			RefreshListView();
			if (value < lvSkills.Items.Count)
			{
				lvSkills.Items[value].Selected = true;
			}
			SavePendingList();
			Console.WriteLine($"[GUI] 执行前自动更新 #{value}: {skillDefinition.SkillId} ({skillDefinition.Name}), maxLevel={skillDefinition.MaxLevel}, common={skillDefinition.Common?.Count ?? 0}");
			return true;
		}
		catch (Exception ex)
		{
			MessageBox.Show("执行前自动保存当前编辑项失败: " + ex.Message, "错误");
			return false;
		}
	}

	private static int ParseMountKnownId(string text)
	{
		if (string.IsNullOrWhiteSpace(text))
			return 0;
		Match match = Regex.Match(text, "\\d+");
		if (!match.Success)
			return 0;
		return int.TryParse(match.Value, out int result) ? result : 0;
	}

	private void LoadCustomMountIds()
	{
		_customMountKnownIds.Clear();
		if (!File.Exists(CustomMountIdsJson))
			return;

		try
		{
			string json = TextFileHelper.ReadAllTextAuto(CustomMountIdsJson);
			List<object> arr = null;
			try
			{
				Dictionary<string, object> obj = SimpleJson.ParseObject(json);
				arr = SimpleJson.GetArray(obj, "mountItemIds");
			}
			catch
			{
			}

			if (arr != null)
			{
				foreach (object item in arr)
				{
					int id = ParseMountIdFromObject(item);
					if (id > 0)
						_customMountKnownIds.Add(id);
				}
			}
			else
			{
				foreach (Match m in Regex.Matches(json ?? "", "\\d+"))
				{
					if (int.TryParse(m.Value, out int id) && id > 0)
						_customMountKnownIds.Add(id);
				}
			}
		}
		catch (Exception ex)
		{
			Console.WriteLine("[坐骑编辑] 读取 custom_mount_ids.json 失败: " + ex.Message);
		}
	}

	private static int ParseMountIdFromObject(object item)
	{
		if (item == null)
			return 0;

		switch (item)
		{
		case int i:
			return i > 0 ? i : 0;
		case long l:
			return (l > 0 && l <= int.MaxValue) ? (int)l : 0;
		case double d:
			return (d > 0 && d <= int.MaxValue) ? (int)d : 0;
		case string s:
			return int.TryParse(s, out int parsed) && parsed > 0 ? parsed : 0;
		default:
			return 0;
		}
	}

	private void SaveCustomMountIds()
	{
		try
		{
			List<int> ids = new List<int>(_customMountKnownIds);
			ids.Sort();

			List<object> arr = new List<object>();
			foreach (int id in ids)
				arr.Add((long)id);

			Dictionary<string, object> root = new Dictionary<string, object>();
			root["mountItemIds"] = arr;

			string dir = Path.GetDirectoryName(CustomMountIdsJson);
			if (!string.IsNullOrWhiteSpace(dir) && !Directory.Exists(dir))
				Directory.CreateDirectory(dir);

			File.WriteAllText(CustomMountIdsJson, SimpleJson.Serialize(root), new UTF8Encoding(false));
		}
		catch (Exception ex)
		{
			Console.WriteLine("[坐骑编辑] 保存 custom_mount_ids.json 失败: " + ex.Message);
		}
	}

	private IEnumerable<int> GetPersistedCustomMountIdsFromActionDir()
	{
		List<int> result = new List<int>();
		try
		{
			string root = PathConfig.GameCharacterTamingMobRoot;
			if (string.IsNullOrWhiteSpace(root) || !Directory.Exists(root))
				return result;

			foreach (string path in Directory.EnumerateFiles(root, "09*.img", SearchOption.TopDirectoryOnly))
			{
				string name = Path.GetFileNameWithoutExtension(path);
				if (string.IsNullOrWhiteSpace(name) || name.Length != 8)
					continue;
				if (!int.TryParse(name, out int mountId))
					continue;
				if (mountId >= 9000000)
					result.Add(mountId);
			}
		}
		catch (Exception ex)
		{
			Console.WriteLine("[坐骑编辑] 扫描坐骑动作目录失败: " + ex.Message);
		}
		return result;
	}

	private void TrackCustomMountId(int mountItemId, bool refresh = false)
	{
		if (mountItemId <= 0)
			return;
		if (_customMountKnownIds.Add(mountItemId))
			SaveCustomMountIds();
		if (refresh)
			RefreshMountKnownIds();
	}

	private void RefreshMountKnownIds()
	{
		if (cboMountKnownIds == null)
			return;

		string oldSelection = cboMountKnownIds.SelectedItem?.ToString() ?? "";
		SortedSet<int> ids = new SortedSet<int>();

		foreach (int mountItemId in _customMountKnownIds)
		{
			if (mountItemId > 0)
				ids.Add(mountItemId);
		}
		foreach (int mountItemId in GetPersistedCustomMountIdsFromActionDir())
		{
			if (mountItemId > 0)
			{
				ids.Add(mountItemId);
				_customMountKnownIds.Add(mountItemId);
			}
		}

		foreach (SkillDefinition pendingSkill in _pendingSkills)
		{
			if (pendingSkill != null && pendingSkill.MountItemId > 0)
				ids.Add(pendingSkill.MountItemId);
		}

		cboMountKnownIds.BeginUpdate();
		try
		{
			cboMountKnownIds.Items.Clear();
			foreach (int id in ids)
			{
				cboMountKnownIds.Items.Add(id + " (" + PathConfig.MountActionImgName(id) + ")");
			}
		}
		finally
		{
			cboMountKnownIds.EndUpdate();
		}

		if (!string.IsNullOrEmpty(oldSelection))
		{
			foreach (object item in cboMountKnownIds.Items)
			{
				if (string.Equals(item?.ToString(), oldSelection, StringComparison.OrdinalIgnoreCase))
				{
					cboMountKnownIds.SelectedItem = item;
					break;
				}
			}
		}

		if (cboMountKnownIds.SelectedIndex < 0 && cboMountKnownIds.Items.Count > 0)
			cboMountKnownIds.SelectedIndex = 0;
	}

	private string InferType(string route, string tab)
	{
		if (tab == "passive")
		{
			return "passive";
		}
		return route switch
		{
			"close_range" => "active_melee", 
			"ranged_attack" => "active_ranged", 
			"magic_attack" => "active_magic", 
			"special_move" => "buff", 
			"skill_effect" => "buff",
			"cancel_buff" => "buff",
			"special_attack" => "active_melee",
			"passive_energy" => "passive",
			_ => "active_melee", 
		};
	}

	private void BtnAddToList_Click(object sender, EventArgs e)
	{
		try
		{
			SkillDefinition skillDefinition = BuildSkillFromForm();
			if (IsCarrierSkillId(skillDefinition.SkillId))
			{
				MessageBox.Show($"技能ID {skillDefinition.SkillId} 是载体技能ID，请在“设置”页维护，不加入列表。", "提示");
				return;
			}
			foreach (SkillDefinition pendingSkill in _pendingSkills)
			{
				if (pendingSkill.SkillId == skillDefinition.SkillId)
				{
					MessageBox.Show($"技能ID {skillDefinition.SkillId} 已在列表中存在", "重复");
					return;
				}
			}
			bool flag = _wzLoader.SkillExistsInImg(skillDefinition.SkillId);
			if (flag)
			{
				DialogResult dialogResult = MessageBox.Show($"技能ID {skillDefinition.SkillId} 已存在于 {skillDefinition.JobId}.img，继续并在执行时覆盖吗？", "ID已存在", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation);
				if (dialogResult != DialogResult.Yes)
				{
					return;
				}
			}
			skillDefinition.ExistsInImg = flag;
			if (flag)
			{
				skillDefinition.SourceLabel = (_wzLoader.IsSuperSkill(skillDefinition.SkillId) ? "超级技能" : "原生技能");
			}
			else
			{
				skillDefinition.SourceLabel = "新技能";
			}
			PushUndoSnapshot();
			RemoveDeletedQueueBySkillId(skillDefinition.SkillId);
			_pendingSkills.Add(skillDefinition);
			_editState.EditingListIndex = null;
			RefreshListView();
			SavePendingList();
			Console.WriteLine($"[GUI] 已添加 {skillDefinition.SkillId} ({skillDefinition.Name})");
		}
		catch (Exception ex)
		{
			MessageBox.Show(ex.Message, "输入错误");
		}
	}

	private void BtnUpdateSelected_Click(object sender, EventArgs e)
	{
		if (!_editState.EditingListIndex.HasValue)
		{
			MessageBox.Show("更新前请先在列表中双击一个技能", "提示");
			return;
		}
		int value = _editState.EditingListIndex.Value;
		if (value < 0 || value >= _pendingSkills.Count)
		{
			return;
		}
		try
		{
			SkillDefinition skillDefinition = BuildSkillFromForm();
			SkillDefinition oldSkill = _pendingSkills[value];
			if (IsCarrierSkillId(skillDefinition.SkillId))
			{
				MessageBox.Show($"技能ID {skillDefinition.SkillId} 是载体技能ID，请在“设置”页维护，不加入列表。", "提示");
				return;
			}
			for (int i = 0; i < _pendingSkills.Count; i++)
			{
				if (i != value && _pendingSkills[i].SkillId == skillDefinition.SkillId)
				{
					MessageBox.Show($"技能ID {skillDefinition.SkillId} 已在列表中存在", "重复");
					return;
				}
			}
			UpdateSourceLabel(skillDefinition);
			PushUndoSnapshot();
			QueueOldSkillWhenIdChanged(oldSkill, skillDefinition);
			RemoveDeletedQueueBySkillId(skillDefinition.SkillId);
			_pendingSkills[value] = skillDefinition;
			RefreshListView();
			SavePendingList();
			if (value < lvSkills.Items.Count)
			{
				lvSkills.Items[value].Selected = true;
			}
			Console.WriteLine($"[GUI] 已更新 {skillDefinition.SkillId} ({skillDefinition.Name})");
		}
		catch (Exception ex)
		{
			MessageBox.Show(ex.Message, "输入错误");
		}
	}

	private void BtnRemoveFromList_Click(object sender, EventArgs e)
	{
		if (lvSkills.SelectedIndices.Count == 0)
		{
			return;
		}
		List<int> selectedIndexes = new List<int>();
		foreach (int selectedIndex in lvSkills.SelectedIndices)
		{
			if (selectedIndex >= 0 && selectedIndex < _pendingSkills.Count)
			{
				selectedIndexes.Add(selectedIndex);
			}
		}
		selectedIndexes = selectedIndexes.Distinct().OrderBy((int i) => i).ToList();
		if (selectedIndexes.Count == 0)
		{
			return;
		}

		string message = (selectedIndexes.Count == 1)
			? $"将技能 {_pendingSkills[selectedIndexes[0]].SkillId} ({_pendingSkills[selectedIndexes[0]].Name}) 从列表移除并加入删除队列吗？"
			: $"将选中的 {selectedIndexes.Count} 个技能从列表移除并加入删除队列吗？";
		DialogResult dialogResult = MessageBox.Show(message, "确认删除", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation);
		if (dialogResult == DialogResult.Yes)
		{
			PushUndoSnapshot();
			HashSet<int> removedSet = new HashSet<int>(selectedIndexes);
			List<int> removedSkillIds = new List<int>();
			for (int i = selectedIndexes.Count - 1; i >= 0; i--)
			{
				int removeIndex = selectedIndexes[i];
				SkillDefinition sd = _pendingSkills[removeIndex];
				QueueDeleteSkill(sd);
				removedSkillIds.Add(sd.SkillId);
				_pendingSkills.RemoveAt(removeIndex);
			}
			if (_editState.EditingListIndex.HasValue)
			{
				int editingIndex = _editState.EditingListIndex.Value;
				if (removedSet.Contains(editingIndex))
				{
					_editState.EditingListIndex = null;
				}
				else
				{
					int shift = selectedIndexes.Count((int idx) => idx < editingIndex);
					_editState.EditingListIndex = editingIndex - shift;
				}
			}
			RefreshListView();
			SavePendingList();
			removedSkillIds.Sort();
			Console.WriteLine($"[GUI] 已标记删除 {removedSkillIds.Count} 个技能: {string.Join(", ", removedSkillIds)}");
		}
	}

	private void BtnClearList_Click(object sender, EventArgs e)
	{
		if (_pendingSkills.Count != 0)
		{
			DialogResult dialogResult = MessageBox.Show($"将列表中的 {_pendingSkills.Count} 个技能全部清空并加入删除队列吗？", "确认清空", MessageBoxButtons.YesNo, MessageBoxIcon.Exclamation);
			if (dialogResult == DialogResult.Yes)
			{
				PushUndoSnapshot();
				QueueDeleteSkills(_pendingSkills);
				_pendingSkills.Clear();
				_editState.EditingListIndex = null;
				RefreshListView();
				SavePendingList();
				Console.WriteLine("[GUI] 已清空列表并加入删除队列");
			}
		}
	}

	private void LvSkills_DoubleClick(object sender, MouseEventArgs e)
	{
		if (lvSkills.SelectedIndices.Count == 0)
		{
			return;
		}
		int num = lvSkills.SelectedIndices[0];
		if (num < 0 || num >= _pendingSkills.Count)
		{
			return;
		}
		SkillDefinition skillDefinition = _pendingSkills[num];
		// Loading an item into editor should not be treated as a manual effect edit.
		// Otherwise clone-save path may wrongly re-encode effect frames even when user changed nothing.
		_hasManualEffectEdit = false;
		Console.WriteLine($"[双击调试] 技能ID={skillDefinition.SkillId} 名称={skillDefinition.Name} 动作={skillDefinition.Action} 类型={skillDefinition.InfoType} 通用参数数={skillDefinition.Common?.Count ?? (-1)} H层级数={skillDefinition.HLevels?.Count ?? (-1)} 等级参数数={skillDefinition.Levels?.Count ?? (-1)} 缓存特效节点数={skillDefinition.CachedEffectsByNode?.Count ?? 0} 缓存当前特效帧数={skillDefinition.CachedEffects?.Count ?? (-1)} 缓存节点树={(skillDefinition.CachedTree != null)}");
		txtSkillId.Text = skillDefinition.SkillId.ToString();
		txtName.Text = skillDefinition.Name;
		txtDesc.Text = skillDefinition.Desc;
		cboTab.SelectedItem = skillDefinition.Tab ?? "active";
		nudMaxLevel.Value = Math.Min(Math.Max(skillDefinition.MaxLevel, 1), 30);
		nudSuperSpCost.Value = Math.Min(Math.Max(skillDefinition.SuperSpCost, 1), 10);
		if (!string.IsNullOrEmpty(skillDefinition.ReleaseType))
		{
			int num2 = Array.IndexOf(RouteNames, skillDefinition.ReleaseType);
			if (num2 >= 0)
			{
				cboPacketRoute.SelectedIndex = num2;
			}
		}
		int num3 = Array.IndexOf(ReleaseClassNames, skillDefinition.ReleaseClass ?? "");
		cboReleaseClass.SelectedIndex = ((num3 >= 0) ? num3 : 0);
		txtProxySkillId.Text = ((skillDefinition.ProxySkillId > 0) ? skillDefinition.ProxySkillId.ToString() : "");
		txtVisualSkillId.Text = ((skillDefinition.VisualSkillId > 0) ? skillDefinition.VisualSkillId.ToString() : "");
		txtMountItemId.Text = ((skillDefinition.MountItemId > 0) ? skillDefinition.MountItemId.ToString() : "");
		SetSelectedMountResourceMode(skillDefinition.MountResourceMode);
		txtMountSourceItemId.Text = ((skillDefinition.MountSourceItemId > 0) ? skillDefinition.MountSourceItemId.ToString() : "");
		txtMountTamingMobId.Text = ((skillDefinition.MountTamingMobId > 0) ? skillDefinition.MountTamingMobId.ToString() : "");
		txtMountSpeed.Text = (skillDefinition.MountSpeedOverride.HasValue ? skillDefinition.MountSpeedOverride.Value.ToString() : "");
		txtMountJump.Text = (skillDefinition.MountJumpOverride.HasValue ? skillDefinition.MountJumpOverride.Value.ToString() : "");
		txtMountFatigue.Text = (skillDefinition.MountFatigueOverride.HasValue ? skillDefinition.MountFatigueOverride.Value.ToString() : "");
		chkBorrowDonorVisual.Checked = skillDefinition.BorrowDonorVisual;
		chkHideFromNative.Checked = skillDefinition.HideFromNativeSkillWnd;
		chkShowInNativeWhenLearned.Checked = skillDefinition.ShowInNativeWhenLearned;
		chkShowInSuperWhenLearned.Checked = skillDefinition.ShowInSuperWhenLearned;
		chkAllowNativeFallback.Checked = skillDefinition.AllowNativeUpgradeFallback;
		chkInjectToNative.Checked = skillDefinition.InjectToNative;
		if (chkAllowMountedFlight != null)
		{
			chkAllowMountedFlight.Checked = skillDefinition.AllowMountedFlight;
		}
		TrySyncMountEditorFromSkill(skillDefinition);
		_editState.LoadFromSkillDefinition(skillDefinition, num);
		SafeSetImage(picIcon, _editState.GetEffectiveIcon());
		SafeSetImage(picIconMO, _editState.GetEffectiveIconMO());
		SafeSetImage(picIconDis, _editState.GetEffectiveIconDis());
		lblAction.Text = (string.IsNullOrEmpty(skillDefinition.Action) ? "" : ("动作: " + skillDefinition.Action));
		WzSkillData wzSkillData = new WzSkillData
		{
			SkillId = skillDefinition.SkillId,
			JobId = skillDefinition.JobId,
			CommonParams = ((skillDefinition.Common != null) ? new Dictionary<string, string>(skillDefinition.Common) : null)
		};
		if (skillDefinition.Levels != null && skillDefinition.Levels.Count > 0)
		{
			wzSkillData.LevelParams = new Dictionary<int, Dictionary<string, string>>();
			foreach (KeyValuePair<int, Dictionary<string, string>> level in skillDefinition.Levels)
			{
				wzSkillData.LevelParams[level.Key] = new Dictionary<string, string>(level.Value);
			}
		}
		PopulateSkillParams(wzSkillData);
		bool flag = false;
		try
		{
			if (_wzLoader.SkillExistsInImg(skillDefinition.SkillId))
			{
				WzSkillData wzSkillData2 = _wzLoader.LoadSkill(skillDefinition.SkillId);
				_editState.LoadFromSkillData(wzSkillData2);
				_editState.EditingListIndex = num;
				// Editing from list should keep queued values as source of truth.
				if (!string.IsNullOrEmpty(skillDefinition.Action))
				{
					_editState.LoadedData.Action = skillDefinition.Action;
				}
				if (skillDefinition.InfoType > 0)
				{
					_editState.LoadedData.InfoType = skillDefinition.InfoType;
				}
				if (skillDefinition.HLevels != null && skillDefinition.HLevels.Count > 0)
				{
					_editState.LoadedData.HLevels.Clear();
					foreach (KeyValuePair<string, string> hLevel in skillDefinition.HLevels)
					{
						_editState.LoadedData.HLevels[hLevel.Key] = hLevel.Value;
					}
				}
				if (!string.IsNullOrEmpty(skillDefinition.PDesc))
				{
					_editState.LoadedData.PDesc = skillDefinition.PDesc;
					_editState.EditedPDesc = skillDefinition.PDesc;
				}
				if (!string.IsNullOrEmpty(skillDefinition.Ph))
				{
					_editState.LoadedData.Ph = skillDefinition.Ph;
					_editState.EditedPh = skillDefinition.Ph;
				}
				if (!string.IsNullOrEmpty(skillDefinition.H))
				{
					_editState.LoadedData.H = skillDefinition.H;
					_editState.EditedH = skillDefinition.H;
				}
				if (skillDefinition.LevelAnimFramesByNode != null && skillDefinition.LevelAnimFramesByNode.Count > 0)
				{
					_editState.EditedLevelAnimFramesByNode = EditState.CloneLevelAnimFramesByNode(skillDefinition.LevelAnimFramesByNode);
				}
				if (skillDefinition.Common != null && skillDefinition.Common.Count > 0)
				{
					if (_editState.EditedLevelParams == null)
					{
						_editState.EditedLevelParams = new Dictionary<int, Dictionary<string, string>>();
					}
					if (!_editState.EditedLevelParams.ContainsKey(0))
					{
						_editState.EditedLevelParams[0] = new Dictionary<string, string>();
					}
					foreach (KeyValuePair<string, string> item in skillDefinition.Common)
					{
						_editState.EditedLevelParams[0][item.Key] = item.Value;
					}
				}
				if (skillDefinition.Levels != null && skillDefinition.Levels.Count > 0)
				{
					if (_editState.EditedLevelParams == null)
					{
						_editState.EditedLevelParams = new Dictionary<int, Dictionary<string, string>>();
					}
					foreach (KeyValuePair<int, Dictionary<string, string>> level2 in skillDefinition.Levels)
					{
						_editState.EditedLevelParams[level2.Key] = new Dictionary<string, string>(level2.Value);
					}
				}
				Dictionary<string, List<WzEffectFrame>> dictionary2 = EditState.CloneEffectsByNode(skillDefinition.CachedEffectsByNode);
				if ((dictionary2 == null || dictionary2.Count == 0) && skillDefinition.CachedEffects != null && skillDefinition.CachedEffects.Count > 0)
				{
					dictionary2 = new Dictionary<string, List<WzEffectFrame>>(StringComparer.OrdinalIgnoreCase)
					{
						["effect"] = EditState.CloneEffectFrameList(skillDefinition.CachedEffects)
					};
				}
				if (dictionary2 != null && dictionary2.Count > 0)
				{
					_editState.EditedEffectsByNode = dictionary2;
				}
				if (skillDefinition.CachedTree != null)
				{
					_editState.EditedTree = skillDefinition.CachedTree;
				}
				if (!string.IsNullOrEmpty(skillDefinition.IconBase64))
				{
					_editState.IconOverride = EditState.BitmapFromBase64(skillDefinition.IconBase64);
				}
				if (!string.IsNullOrEmpty(skillDefinition.IconMouseOverBase64))
				{
					_editState.IconMOOverride = EditState.BitmapFromBase64(skillDefinition.IconMouseOverBase64);
				}
				if (!string.IsNullOrEmpty(skillDefinition.IconDisabledBase64))
				{
					_editState.IconDisOverride = EditState.BitmapFromBase64(skillDefinition.IconDisabledBase64);
				}
				SafeSetImage(picIcon, _editState.GetEffectiveIcon());
				SafeSetImage(picIconMO, _editState.GetEffectiveIconMO());
				SafeSetImage(picIconDis, _editState.GetEffectiveIconDis());
				PopulateTreeView(_editState.EditedTree ?? wzSkillData2.RootNode);
				Dictionary<int, Dictionary<string, string>> dictionary = null;
				if (_editState.EditedLevelParams != null)
				{
					foreach (KeyValuePair<int, Dictionary<string, string>> editedLevelParam in _editState.EditedLevelParams)
					{
						if (editedLevelParam.Key >= 1)
						{
							if (dictionary == null)
							{
								dictionary = new Dictionary<int, Dictionary<string, string>>();
							}
							dictionary[editedLevelParam.Key] = new Dictionary<string, string>(editedLevelParam.Value);
						}
					}
				}
				WzSkillData data = new WzSkillData
				{
					SkillId = skillDefinition.SkillId,
					JobId = skillDefinition.JobId,
					CommonParams = ((_editState.EditedLevelParams != null && _editState.EditedLevelParams.ContainsKey(0)) ? new Dictionary<string, string>(_editState.EditedLevelParams[0]) : wzSkillData2.CommonParams),
					LevelParams = (dictionary ?? wzSkillData2.LevelParams)
				};
				PopulateSkillParams(data);
				RefreshAnimLevelSelector(preserveSelection: false);
				RefreshEffectNodeSelector("effect", createIfMissing: true);
				PopulateEffectFrames(_editState.EditedEffects);
				PopulateTextFields();
				if (!string.IsNullOrEmpty(_editState.LoadedData?.Action))
				{
					lblAction.Text = "动作: " + _editState.LoadedData.Action;
				}
				flag = true;
			}
		}
		catch (Exception ex)
		{
			Console.WriteLine("[GUI] 加载技能详情失败: " + ex.Message);
		}
		if (!flag)
		{
			if (_editState.EditedTree != null)
			{
				PopulateTreeView(_editState.EditedTree);
			}
			else
			{
				treeSkillData.Nodes.Clear();
			}
			RefreshAnimLevelSelector(preserveSelection: false);
			RefreshEffectNodeSelector("effect", createIfMissing: true);
			if (_editState.EditedEffects != null && _editState.EditedEffects.Count > 0)
			{
				PopulateEffectFrames(_editState.EditedEffects);
			}
			else
			{
				PopulateEffectFrames(null);
			}
			PopulateTextFields();
		}
		for (int i = 0; i < lvSkills.Items.Count; i++)
		{
			lvSkills.Items[i].BackColor = ((i == num) ? Color.FromArgb(60, 80, 60) : SystemColors.Window);
		}
		Console.WriteLine($"[GUI] 正在编辑列表项 #{num}: {skillDefinition.SkillId} ({skillDefinition.Name})");
	}

	private void UpdateSourceLabel(SkillDefinition sd)
	{
		try
		{
			if (sd.ExistsInImg = _wzLoader.SkillExistsInImg(sd.SkillId))
			{
				sd.SourceLabel = (_wzLoader.IsSuperSkill(sd.SkillId) ? "超级技能" : "原生技能");
			}
			else
			{
				sd.SourceLabel = "新技能";
			}
		}
		catch
		{
			sd.ExistsInImg = false;
			sd.SourceLabel = "新技能";
		}
	}

	private void RefreshListView()
	{
		lvSkills.Items.Clear();
		for (int i = 0; i < _pendingSkills.Count; i++)
		{
			SkillDefinition skillDefinition = _pendingSkills[i];
			string text = skillDefinition.SourceLabel ?? "新技能";
			string text2 = (skillDefinition.ExistsInImg ? "修改" : "新增");
			ListViewItem listViewItem = new ListViewItem(skillDefinition.SkillId.ToString());
			listViewItem.SubItems.Add(skillDefinition.Name);
			listViewItem.SubItems.Add(skillDefinition.Tab);
			listViewItem.SubItems.Add(skillDefinition.ReleaseType ?? "");
			listViewItem.SubItems.Add((skillDefinition.ProxySkillId > 0) ? skillDefinition.ProxySkillId.ToString() : "");
			listViewItem.SubItems.Add(skillDefinition.MaxLevel.ToString());
			listViewItem.SubItems.Add(skillDefinition.SuperSpCost.ToString());
			listViewItem.SubItems.Add(text);
			listViewItem.SubItems.Add(text2);
			listViewItem.SubItems.Add((_editState.EditingListIndex == i) ? "编辑中" : "");
			if (text == "原生技能")
			{
				listViewItem.ForeColor = Color.FromArgb(255, 200, 100);
			}
			else if (text == "超级技能")
			{
				listViewItem.ForeColor = Color.FromArgb(100, 200, 255);
			}
			lvSkills.Items.Add(listViewItem);
		}
		RefreshMountKnownIds();
	}

	private void BtnExecuteAdd_Click(object sender, EventArgs e)
	{
		if (!TryAutoCommitEditingSkill())
		{
			return;
		}
		if (_pendingSkills.Count == 0 && _deletedSkills.Count == 0)
		{
			MessageBox.Show("列表为空，且没有待删除技能", "提示");
			return;
		}
		string text = "";
		if (_pendingSkills.Count > 0)
		{
			text += $"新增 {_pendingSkills.Count} 个技能";
		}
		if (_deletedSkills.Count > 0)
		{
			if (text.Length > 0)
			{
				text += ", ";
			}
			text += $"删除 {_deletedSkills.Count} 个技能";
		}
		if (MessageBox.Show("确认执行：" + text + "?", "确认", MessageBoxButtons.YesNo) == DialogResult.Yes)
		{
			if (!TryBeginExecuteTask())
			{
				return;
			}
			PushUndoSnapshot();
			bool skip = chkSkipImg.Checked;
			List<SkillDefinition> skills = new List<SkillDefinition>(_pendingSkills);
			List<SkillDefinition> deleted = new List<SkillDefinition>(_deletedSkills);
			RunInThread(delegate
			{
				try
				{
					ExecuteAddAndDelete(skills, deleted, dryRun: false, skip);
				}
				finally
				{
					EndExecuteTask();
				}
			});
		}
	}

	private void BtnDryRun_Click(object sender, EventArgs e)
	{
		if (!TryAutoCommitEditingSkill())
		{
			return;
		}
		if (_pendingSkills.Count == 0 && _deletedSkills.Count == 0)
		{
			MessageBox.Show("列表为空，且没有待删除技能", "提示");
			return;
		}
		bool skip = chkSkipImg.Checked;
		List<SkillDefinition> skills = new List<SkillDefinition>(_pendingSkills);
		List<SkillDefinition> deleted = new List<SkillDefinition>(_deletedSkills);
		if (!TryBeginExecuteTask())
		{
			return;
		}
		RunInThread(delegate
		{
			try
			{
				ExecuteAddAndDelete(skills, deleted, dryRun: true, skip);
			}
			finally
			{
				EndExecuteTask();
			}
		});
	}

	private void ExecuteAddAndDelete(List<SkillDefinition> skills, List<SkillDefinition> deletedSkills, bool dryRun, bool skipImg)
	{
		bool flag = false;
		TextWriter textWriter = Console.Out;
		StringWriter stringWriter = new StringWriter();
		TeeWriter teeWriter = new TeeWriter(textWriter, stringWriter);
		Console.SetOut(teeWriter);
		try
		{
			if (!dryRun)
			{
				_wzLoader.ClearCache();
			}
			Console.WriteLine("================================================================");
			Console.WriteLine(dryRun ? "  演练" : "  正式执行");
			Console.WriteLine("  引擎版本: 2026-04-12-png-raw-preserve");
			Console.WriteLine("  运行目录: " + AppDomain.CurrentDomain.BaseDirectory);
			Console.WriteLine("================================================================");
			if (skills.Count > 0)
			{
				Console.WriteLine($"处理 {skills.Count} 个待新增技能...");
				if (!Directory.Exists(PathConfig.OutputDir))
				{
					Directory.CreateDirectory(PathConfig.OutputDir);
				}
				if (!skipImg)
				{
					ServerXmlGenerator.Generate(skills, dryRun);
					ServerStringXmlGenerator.Generate(skills, dryRun);
				}
				else
				{
					Console.WriteLine("\n[ServerXml] 已跳过");
					Console.WriteLine("[ServerStringXml] 已跳过");
				}
				if (!dryRun)
				{
					_wzLoader.ClearCache();
				}
				ImgWriteGenerator.Generate(skills, dryRun);
				DllJsonGenerator.GenerateSkillImgJson(skills, dryRun);
				DllJsonGenerator.GenerateStringImgJson(skills, dryRun);
				MountResourceGenerator.Generate(skills, dryRun);
				ConfigJsonGenerator.Generate(skills, dryRun);
				SqlGenerator.Generate(skills, dryRun);
				ChecklistGenerator.Generate(skills, dryRun);
				HarepackerGuideGenerator.Generate(skills, dryRun);
			}
			if (deletedSkills.Count > 0)
			{
				Console.WriteLine($"\n处理 {deletedSkills.Count} 个待删除技能...");
				if (!skipImg)
				{
					ServerXmlGenerator.Remove(deletedSkills, dryRun);
					ServerStringXmlGenerator.Remove(deletedSkills, dryRun);
				}
				if (!dryRun)
				{
					_wzLoader.ClearCache();
				}
				ImgDeleteGenerator.Delete(deletedSkills, dryRun);
				DllJsonGenerator.RemoveSkillImgJson(deletedSkills, dryRun);
				DllJsonGenerator.RemoveStringImgJson(deletedSkills, dryRun);
				ConfigJsonGenerator.Remove(deletedSkills, dryRun);
			}
			Console.WriteLine("\n================================================================");
		}
		catch (Exception ex)
		{
			flag = true;
			Console.WriteLine("\n  错误: " + ex.Message + "\n" + ex.StackTrace);
		}
		Console.SetOut(textWriter);
		string text = stringWriter.ToString();
		if (!flag && text.Contains("[error]"))
		{
			flag = true;
		}
		if (dryRun)
		{
			Console.WriteLine("  演练完成");
		}
		else if (flag)
		{
			Console.WriteLine("  部分操作失败，请查看日志");
		}
		else
		{
			Console.WriteLine("  全部完成!");
			Console.WriteLine("  备份: " + BackupHelper.GetSessionBackupDir());
			Console.WriteLine("  输出: " + PathConfig.OutputDir);
		}
		Console.WriteLine("================================================================");
		if (dryRun)
		{
			return;
		}
		SafeInvoke(delegate
		{
			if (!flag)
			{
				_deletedSkills.Clear();
			}
		});
		SafeInvoke(delegate
		{
			foreach (SkillDefinition pendingSkill in _pendingSkills)
			{
				UpdateSourceLabel(pendingSkill);
			}
			RefreshListView();
			SavePendingList();
		});
		if (flag)
		{
			SafeInvoke(delegate
			{
				MessageBox.Show("部分操作失败，请查看日志", "警告", MessageBoxButtons.OK, MessageBoxIcon.Exclamation);
			});
		}
		else
		{
			SafeInvoke(delegate
			{
				MessageBox.Show("全部完成!", "超级技能工具");
			});
		}
	}

	private void BtnImportJson_Click(object sender, EventArgs e)
	{
		OpenFileDialog openFileDialog = new OpenFileDialog
		{
			Filter = "JSON|*.json|全部文件|*.*",
			InitialDirectory = PathConfig.ToolRoot
		};
		try
		{
			if (openFileDialog.ShowDialog() != DialogResult.OK)
			{
				return;
			}
			try
			{
				List<SkillDefinition> list = SkillDefinition.LoadFromFile(openFileDialog.FileName) ?? new List<SkillDefinition>();
				HashSet<int> hashSet = new HashSet<int>((_pendingSkills ?? new List<SkillDefinition>()).Where((SkillDefinition s) => s != null).Select((SkillDefinition s) => s.SkillId));
				List<SkillDefinition> list2 = new List<SkillDefinition>();
				foreach (SkillDefinition item in list)
				{
					if (IsCarrierSkillId(item.SkillId))
					{
						continue;
					}
					ApplyConfigHints(item);
					UpdateSourceLabel(item);
					if (hashSet.Add(item.SkillId))
					{
						list2.Add(item);
					}
				}
				if (list2.Count == 0)
				{
					MessageBox.Show("导入成功，但所有技能ID都已在列表中", "提示");
					return;
				}
				PushUndoSnapshot();
				foreach (SkillDefinition item2 in list2)
				{
					RemoveDeletedQueueBySkillId(item2.SkillId);
				}
				_pendingSkills.AddRange(list2);
				RefreshListView();
				SavePendingList();
				tabMain.SelectedIndex = 0;
				Console.WriteLine($"[GUI] 已导入 {list2.Count} 个技能");
			}
			catch (Exception ex)
			{
				MessageBox.Show("导入失败: " + ex.Message, "错误");
			}
		}
		finally
		{
			((IDisposable)(object)openFileDialog)?.Dispose();
		}
	}

	private void BtnExportJson_Click(object sender, EventArgs e)
	{
		if (_pendingSkills.Count == 0)
		{
			MessageBox.Show("列表为空", "提示");
			return;
		}
		SaveFileDialog saveFileDialog = new SaveFileDialog
		{
			Filter = "JSON|*.json",
			FileName = "exported_skills.json",
			InitialDirectory = PathConfig.OutputDir
		};
		try
		{
			if (saveFileDialog.ShowDialog() != DialogResult.OK)
			{
				return;
			}
			try
			{
				File.WriteAllText(saveFileDialog.FileName, SkillDefinition.SerializeList(_pendingSkills), new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
				Console.WriteLine($"[GUI] 已导出 {_pendingSkills.Count} 个技能");
			}
			catch (Exception ex)
			{
				MessageBox.Show("导出失败: " + ex.Message, "错误");
			}
		}
		finally
		{
			((IDisposable)(object)saveFileDialog)?.Dispose();
		}
	}

	private void BtnSaveSettings_Click(object sender, EventArgs e)
	{
		PathConfig.ServerRootDir = txtServerRootDir.Text.Trim();
		PathConfig.GameDataBaseDir = txtGameDataBaseDir.Text.Trim();
		PathConfig.OutputDir = txtOutputDir.Text.Trim();
		string newConfigDir = (txtConfigDataDir?.Text ?? "").Trim();
		if (!string.IsNullOrWhiteSpace(newConfigDir))
			PathConfig.ConfigDataDir = newConfigDir;
		if (string.IsNullOrWhiteSpace(PathConfig.OutputDir))
		{
			PathConfig.OutputDir = Path.Combine(PathConfig.ServerRootDir, "output");
		}
		if (!TryApplyCarrierSkillId(txtDefaultCarrierId, showError: true))
		{
			return;
		}
		PathConfig.RecomputeDerivedPaths();
		txtServerRootDir.Text = PathConfig.ServerRootDir;
		txtGameDataBaseDir.Text = PathConfig.GameDataBaseDir;
		txtOutputDir.Text = PathConfig.OutputDir;
		if (txtConfigDataDir != null) txtConfigDataDir.Text = PathConfig.ConfigDataDir;
		LoadConfigSnapshots();
		RefreshJobIdComboItems();
		SettingsManager.Save();
		LoadSkillLibrary(forceReload: true);
		SyncCarrierSkillIdEditors();
		Console.WriteLine("[GUI] 设置已保存");
		MessageBox.Show("设置已保存", "完成");
	}

	private void BtnResetSettings_Click(object sender, EventArgs e)
	{
		txtServerRootDir.Text = "G:\\code\\dasheng099";
		txtGameDataBaseDir.Text = "G:\\code\\mxd\\Data";
		txtOutputDir.Text = Path.Combine(txtServerRootDir.Text.Trim(), "output");
		if (txtConfigDataDir != null) txtConfigDataDir.Text = PathConfig.ToolRoot;
		txtDefaultCarrierId.Text = "1001038";
		SyncCarrierSkillIdEditors(txtDefaultCarrierId);
	}

	private void RunInThread(Action action)
	{
		ThreadPool.QueueUserWorkItem(delegate
		{
			action();
		});
	}

	private bool TryBeginExecuteTask()
	{
		if (Interlocked.CompareExchange(ref _executeBusy, 1, 0) != 0)
		{
			SafeInvoke(delegate
			{
				MessageBox.Show("已有执行任务正在进行，请稍候", "提示");
			});
			return false;
		}
		SafeInvoke(delegate
		{
			if (btnExecuteAdd != null)
			{
				btnExecuteAdd.Enabled = false;
			}
		});
		return true;
	}

	private void EndExecuteTask()
	{
		Interlocked.Exchange(ref _executeBusy, 0);
		SafeInvoke(delegate
		{
			if (btnExecuteAdd != null)
			{
				btnExecuteAdd.Enabled = true;
			}
		});
	}

	private void SafeInvoke(Action action)
	{
		if (base.InvokeRequired)
		{
			Invoke(action);
		}
		else
		{
			action();
		}
	}

	private string ShowInputBox(string title, string prompt, string defaultValue)
	{
		Form form = new Form
		{
			Text = title,
			Size = new Size(420, 170),
			StartPosition = FormStartPosition.CenterParent,
			FormBorderStyle = FormBorderStyle.FixedDialog,
			MaximizeBox = false,
			MinimizeBox = false
		};
		Label label = new Label
		{
			Text = prompt,
			Location = new Point(10, 10),
			MaximumSize = new Size(390, 0),
			AutoSize = true
		};
		int inputY = Math.Max(50, label.PreferredHeight + 18);
		TextBox textBox = new TextBox
		{
			Location = new Point(10, inputY),
			Width = 380,
			Text = defaultValue
		};
		int btnY = inputY + 35;
		Button button = new Button
		{
			Text = "确定",
			Location = new Point(220, btnY),
			Width = 80,
			DialogResult = DialogResult.OK
		};
		Button button2 = new Button
		{
			Text = "取消",
			Location = new Point(310, btnY),
			Width = 80,
			DialogResult = DialogResult.Cancel
		};
		form.ClientSize = new Size(400, btnY + 40);
		form.Controls.AddRange(label, textBox, button, button2);
		form.AcceptButton = button;
		form.CancelButton = button2;
		return (form.ShowDialog() == DialogResult.OK) ? textBox.Text : null;
	}

	private string ShowMultiLineInputBox(string title, string prompt, string defaultValue)
	{
		Form form = new Form
		{
			Text = title,
			Size = new Size(760, 620),
			StartPosition = FormStartPosition.CenterParent,
			FormBorderStyle = FormBorderStyle.Sizable,
			MinimizeBox = false,
			KeyPreview = true
		};
		Label label = new Label
		{
			Text = prompt,
			Location = new Point(12, 10),
			MaximumSize = new Size(720, 0),
			AutoSize = true
		};
		int editorTop = label.Bottom + 8;
		TextBox textBox = new TextBox
		{
			Location = new Point(12, editorTop),
			Size = new Size(720, 520 - editorTop),
			Multiline = true,
			ScrollBars = ScrollBars.Both,
			WordWrap = false,
			AcceptsTab = true,
			AcceptsReturn = true,
			Font = new Font("Consolas", 10f),
			Text = defaultValue ?? ""
		};
		textBox.Anchor = (AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right);
		Button button = new Button
		{
			Text = "确定",
			Location = new Point(562, 548),
			Width = 80,
			DialogResult = DialogResult.OK
		};
		button.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
		Button button2 = new Button
		{
			Text = "取消",
			Location = new Point(652, 548),
			Width = 80,
			DialogResult = DialogResult.Cancel
		};
		button2.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
		form.ClientSize = new Size(744, 582);
		textBox.KeyDown += delegate(object sender, KeyEventArgs e)
		{
			if (e.Control && e.KeyCode == Keys.Enter)
			{
				e.SuppressKeyPress = true;
				form.DialogResult = DialogResult.OK;
				form.Close();
			}
		};
		form.KeyDown += delegate(object sender, KeyEventArgs e)
		{
			if (e.KeyCode == Keys.Escape)
			{
				e.SuppressKeyPress = true;
				form.DialogResult = DialogResult.Cancel;
				form.Close();
			}
		};
		form.Controls.AddRange(label, textBox, button, button2);
		form.CancelButton = button2;
		return (form.ShowDialog() == DialogResult.OK) ? textBox.Text : null;
	}

	private void LoadConfigSnapshots()
	{
		_superSkillsCfgById = LoadSkillIdMap(PathConfig.SuperSkillsJson, "skills");
		_routesCfgById = LoadSkillIdMap(PathConfig.CustomSkillRoutesJson, "routes");
		_injectionsCfgById = LoadSkillIdMap(PathConfig.NativeSkillInjectionsJson, "skills");
		_serverCfgById = LoadSkillIdMap(PathConfig.SuperSkillsServerJson, "skills");
	}

	private static Dictionary<int, Dictionary<string, object>> LoadSkillIdMap(string path, string arrayName)
	{
		Dictionary<int, Dictionary<string, object>> dictionary = new Dictionary<int, Dictionary<string, object>>();
		if (!File.Exists(path))
		{
			return dictionary;
		}
		try
		{
			string json = TextFileHelper.ReadAllTextAuto(path);
			Dictionary<string, object> obj = SimpleJson.ParseObject(json);
			List<object> array = SimpleJson.GetArray(obj, arrayName);
			if (array == null)
			{
				return dictionary;
			}
			foreach (object item in array)
			{
				if (item is Dictionary<string, object> dictionary2)
				{
					int num = SimpleJson.GetInt(dictionary2, "skillId", -1);
					if (num > 0)
					{
						dictionary[num] = dictionary2;
					}
				}
			}
		}
		catch
		{
		}
		return dictionary;
	}

	private void ApplyConfigHints(SkillDefinition sd)
	{
		if (sd == null || sd.SkillId <= 0)
		{
			return;
		}
		if (_superSkillsCfgById.TryGetValue(sd.SkillId, out var value))
		{
			if (value.ContainsKey("tab"))
			{
				sd.Tab = SimpleJson.GetString(value, "tab", sd.Tab);
			}
			if (value.ContainsKey("superSpCost"))
			{
				sd.SuperSpCost = SimpleJson.GetInt(value, "superSpCost", sd.SuperSpCost);
			}
			if (value.ContainsKey("hideFromNativeSkillWnd"))
			{
				sd.HideFromNativeSkillWnd = SimpleJson.GetBool(value, "hideFromNativeSkillWnd", sd.HideFromNativeSkillWnd);
			}
			if (value.ContainsKey("showInNativeWhenLearned"))
			{
				sd.ShowInNativeWhenLearned = SimpleJson.GetBool(value, "showInNativeWhenLearned", sd.ShowInNativeWhenLearned);
			}
			if (value.ContainsKey("showInSuperWhenLearned"))
			{
				sd.ShowInSuperWhenLearned = SimpleJson.GetBool(value, "showInSuperWhenLearned", sd.ShowInSuperWhenLearned);
			}
			if (value.ContainsKey("allowNativeUpgradeFallback"))
			{
				sd.AllowNativeUpgradeFallback = SimpleJson.GetBool(value, "allowNativeUpgradeFallback", sd.AllowNativeUpgradeFallback);
			}
			if (sd.SuperSpCarrierSkillId <= 0 && value.ContainsKey("superSpCarrierSkillId"))
			{
				sd.SuperSpCarrierSkillId = SimpleJson.GetInt(value, "superSpCarrierSkillId", 0);
			}
		}
		if (_routesCfgById.TryGetValue(sd.SkillId, out var value2))
		{
			if (value2.ContainsKey("packetRoute"))
			{
				string text = SimpleJson.GetString(value2, "packetRoute", sd.ReleaseType);
				if (!string.IsNullOrEmpty(text))
				{
					sd.ReleaseType = text;
				}
			}
			if (sd.ProxySkillId <= 0 && value2.ContainsKey("proxySkillId"))
			{
				sd.ProxySkillId = SimpleJson.GetInt(value2, "proxySkillId", 0);
			}
			if (sd.VisualSkillId <= 0 && value2.ContainsKey("visualSkillId"))
			{
				sd.VisualSkillId = SimpleJson.GetInt(value2, "visualSkillId", 0);
			}
			if (value2.ContainsKey("releaseClass"))
			{
				sd.ReleaseClass = SimpleJson.GetString(value2, "releaseClass", sd.ReleaseClass);
			}
			if (value2.ContainsKey("borrowDonorVisual"))
			{
				sd.BorrowDonorVisual = SimpleJson.GetBool(value2, "borrowDonorVisual", sd.BorrowDonorVisual);
			}
		}
		if (_injectionsCfgById.TryGetValue(sd.SkillId, out var value3))
		{
			sd.InjectToNative = true;
			if (value3.ContainsKey("donorSkillId"))
			{
				sd.DonorSkillId = SimpleJson.GetInt(value3, "donorSkillId", sd.DonorSkillId);
			}
			if (value3.ContainsKey("enabled"))
			{
				sd.InjectEnabled = SimpleJson.GetBool(value3, "enabled", sd.InjectEnabled);
			}
		}
		if (_serverCfgById.TryGetValue(sd.SkillId, out var value4))
		{
			if (value4.ContainsKey("superSpCost"))
			{
				sd.SuperSpCost = SimpleJson.GetInt(value4, "superSpCost", sd.SuperSpCost);
			}
			if (sd.SuperSpCarrierSkillId <= 0 && value4.ContainsKey("superSpCarrierSkillId"))
			{
				sd.SuperSpCarrierSkillId = SimpleJson.GetInt(value4, "superSpCarrierSkillId", sd.SuperSpCarrierSkillId);
			}
			if (value4.ContainsKey("enabled"))
			{
				sd.ServerEnabled = SimpleJson.GetBool(value4, "enabled", sd.ServerEnabled);
			}
			if (sd.DonorSkillId <= 0 && value4.ContainsKey("behaviorSkillId"))
			{
				sd.DonorSkillId = SimpleJson.GetInt(value4, "behaviorSkillId", sd.DonorSkillId);
			}
			if (sd.MountItemId <= 0 && value4.ContainsKey("mountItemId"))
			{
				sd.MountItemId = SimpleJson.GetInt(value4, "mountItemId", sd.MountItemId);
			}
			if (value4.ContainsKey("allowMountedFlight") || value4.ContainsKey("grantSoaringOnRide"))
			{
				sd.AllowMountedFlight = SimpleJson.GetBool(
					value4,
					"allowMountedFlight",
					SimpleJson.GetBool(value4, "grantSoaringOnRide", sd.AllowMountedFlight));
			}
		}
		if (string.IsNullOrEmpty(sd.ReleaseClass))
		{
			sd.ReleaseClass = ReleaseClassNames[0];
		}
	}

	private string GetSelectedMountResourceMode()
	{
		if (cboMountResourceMode == null)
		{
			return MountResourceModeNames[0];
		}
		int selectedIndex = cboMountResourceMode.SelectedIndex;
		if (selectedIndex >= 0 && selectedIndex < MountResourceModeNames.Length)
		{
			return MountResourceModeNames[selectedIndex];
		}
		return MountResourceModeNames[0];
	}

	private void SetSelectedMountResourceMode(string mode)
	{
		if (cboMountResourceMode == null)
		{
			return;
		}
		string text = NormalizeMountResourceMode(mode);
		int num = Array.IndexOf(MountResourceModeNames, text);
		cboMountResourceMode.SelectedIndex = ((num >= 0) ? num : 0);
	}

	private static string NormalizeMountResourceMode(string mode)
	{
		string text = (mode ?? "").Trim().ToLowerInvariant();
		switch (text)
		{
		case "sync_action":
		case "action":
		case "clone_action":
			return "sync_action";
		case "sync_action_and_data":
		case "action_data":
		case "clone_action_and_data":
		case "sync_all":
			return "sync_action_and_data";
		default:
			return "config_only";
		}
	}

	private static bool TryParseOptionalInt(string text, out int value)
	{
		value = 0;
		if (string.IsNullOrWhiteSpace(text))
		{
			return false;
		}
		return int.TryParse(text.Trim(), out value);
	}

	private int ResolveMountSourceHint(SkillDefinition sd)
	{
		if (sd == null)
		{
			return 0;
		}
		if (sd.MountSourceItemId > 0)
		{
			return sd.MountSourceItemId;
		}
		if (sd.DonorSkillId > 0
			&& _serverCfgById != null
			&& _serverCfgById.TryGetValue(sd.DonorSkillId, out var value)
			&& value != null
			&& value.ContainsKey("mountItemId"))
		{
			int num = SimpleJson.GetInt(value, "mountItemId", 0);
			if (num > 0 && num != sd.MountItemId)
			{
				return num;
			}
		}
		if (_serverCfgById != null
			&& _serverCfgById.TryGetValue(sd.SkillId, out var value2)
			&& value2 != null
			&& value2.ContainsKey("mountItemId"))
		{
			int num2 = SimpleJson.GetInt(value2, "mountItemId", 0);
			if (num2 > 0 && num2 != sd.MountItemId)
			{
				return num2;
			}
		}
		return 0;
	}

	private void SavePendingList()
	{
		try
		{
			_pendingSkills ??= new List<SkillDefinition>();
			RemoveCarrierSkillsFromQueues();
			if (File.Exists(PendingSkillsJson))
			{
				BackupHelper.Backup(PendingSkillsJson);
			}
			string dir = Path.GetDirectoryName(PendingSkillsJson);
			if (!string.IsNullOrWhiteSpace(dir) && !Directory.Exists(dir))
			{
				Directory.CreateDirectory(dir);
			}
			File.WriteAllText(PendingSkillsJson, SkillDefinition.SerializeList(_pendingSkills), new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
		}
		catch (Exception ex)
		{
			Console.WriteLine("[持久化] 保存失败: " + ex.Message);
		}
	}

	private void LoadPendingFromJson()
	{
		if (!File.Exists(PendingSkillsJson))
		{
			return;
		}
		try
		{
			List<SkillDefinition> list = SkillDefinition.LoadFromFile(PendingSkillsJson) ?? new List<SkillDefinition>();
			int num = 0;
			foreach (SkillDefinition item in list)
			{
				if (IsCarrierSkillId(item.SkillId))
				{
					continue;
				}
				ApplyConfigHints(item);
				UpdateSourceLabel(item);
				num++;
			}
			_pendingSkills ??= new List<SkillDefinition>();
			_pendingSkills.AddRange(list.Where((SkillDefinition s) => s != null && !IsCarrierSkillId(s.SkillId)));
			if (num > 0)
			{
				RefreshListView();
				Console.WriteLine($"[启动] 已从JSON加载 {num} 个技能");
			}
		}
		catch (Exception ex)
		{
			Console.WriteLine("[启动] 读取JSON失败: " + ex.Message);
		}
	}

	private void ReloadSelectedFromImg()
	{
		if (lvSkills.SelectedIndices.Count == 0)
		{
			return;
		}
		int num = lvSkills.SelectedIndices[0];
		if (num < 0 || num >= _pendingSkills.Count)
		{
			return;
		}
		SkillDefinition skillDefinition = _pendingSkills[num];
		try
		{
			if (!_wzLoader.SkillExistsInImg(skillDefinition.SkillId))
			{
				MessageBox.Show($"技能 {skillDefinition.SkillId} 在 .img 中不存在", "提示");
				return;
			}
			WzSkillData wzSkillData = _wzLoader.LoadSkill(skillDefinition.SkillId);
			PushUndoSnapshot();
			skillDefinition.Name = ((!string.IsNullOrEmpty(wzSkillData.Name)) ? wzSkillData.Name : skillDefinition.Name);
			skillDefinition.Desc = wzSkillData.Desc ?? skillDefinition.Desc;
			skillDefinition.PDesc = wzSkillData.PDesc ?? skillDefinition.PDesc;
			skillDefinition.Ph = wzSkillData.Ph ?? skillDefinition.Ph;
			skillDefinition.IconBase64 = wzSkillData.IconBase64 ?? skillDefinition.IconBase64;
			skillDefinition.IconMouseOverBase64 = wzSkillData.IconMouseOverBase64 ?? skillDefinition.IconMouseOverBase64;
			skillDefinition.IconDisabledBase64 = wzSkillData.IconDisabledBase64 ?? skillDefinition.IconDisabledBase64;
			skillDefinition.Action = wzSkillData.Action ?? skillDefinition.Action;
			skillDefinition.InfoType = wzSkillData.InfoType;
			skillDefinition.MaxLevel = ResolveMaxLevelFromWzData(wzSkillData, skillDefinition.MaxLevel);
			if (wzSkillData.CommonParams != null)
			{
				skillDefinition.Common.Clear();
				foreach (KeyValuePair<string, string> commonParam in wzSkillData.CommonParams)
				{
					skillDefinition.Common[commonParam.Key] = commonParam.Value;
				}
			}
			if (wzSkillData.LevelParams != null && wzSkillData.LevelParams.Count > 0)
			{
				skillDefinition.Levels = new Dictionary<int, Dictionary<string, string>>();
				foreach (KeyValuePair<int, Dictionary<string, string>> levelParam in wzSkillData.LevelParams)
				{
					skillDefinition.Levels[levelParam.Key] = new Dictionary<string, string>(levelParam.Value);
				}
			}
			if (wzSkillData.HLevels != null)
			{
				skillDefinition.HLevels.Clear();
				foreach (KeyValuePair<string, string> hLevel in wzSkillData.HLevels)
				{
					skillDefinition.HLevels[hLevel.Key] = hLevel.Value;
				}
			}
			ApplyConfigHints(skillDefinition);
			EnsureRecommendedRoute(skillDefinition, wzSkillData);
			RefreshListView();
			SavePendingList();
			LvSkills_DoubleClick(lvSkills, new MouseEventArgs(MouseButtons.Left, 2, 0, 0, 0));
			Console.WriteLine($"[GUI] 已从 .img 重新加载 {skillDefinition.SkillId} ({skillDefinition.Name})");
		}
		catch (Exception ex)
		{
			MessageBox.Show("重新加载失败: " + ex.Message, "错误");
		}
	}

	private void LoadSuperSkillsFromImg()
	{
		try
		{
			int[] jobIds = (JobList ?? new List<KeyValuePair<string, int>>()).Select((KeyValuePair<string, int> kv) => kv.Value).Distinct().ToArray();
			List<WzSkillData> list = _wzLoader.ScanSuperSkills(jobIds);
			if (list.Count == 0)
			{
				return;
			}
			_pendingSkills ??= new List<SkillDefinition>();
			int num = 0;
			foreach (WzSkillData data in list)
			{
				if (IsCarrierSkillId(data.SkillId))
				{
					continue;
				}
				num++;
				SkillDefinition skillDefinition = _pendingSkills.FirstOrDefault((SkillDefinition s) => s != null && s.SkillId == data.SkillId);
				if (skillDefinition != null)
				{
					skillDefinition.Name = ((!string.IsNullOrEmpty(data.Name)) ? data.Name : skillDefinition.Name);
					skillDefinition.Desc = data.Desc ?? skillDefinition.Desc;
					skillDefinition.PDesc = data.PDesc ?? skillDefinition.PDesc;
					skillDefinition.Ph = data.Ph ?? skillDefinition.Ph;
					skillDefinition.IconBase64 = data.IconBase64 ?? skillDefinition.IconBase64;
					skillDefinition.IconMouseOverBase64 = data.IconMouseOverBase64 ?? skillDefinition.IconMouseOverBase64;
					skillDefinition.IconDisabledBase64 = data.IconDisabledBase64 ?? skillDefinition.IconDisabledBase64;
					skillDefinition.Action = data.Action ?? skillDefinition.Action;
					skillDefinition.InfoType = data.InfoType;
					skillDefinition.MaxLevel = ResolveMaxLevelFromWzData(data, skillDefinition.MaxLevel);
					if (data.CommonParams != null)
					{
						skillDefinition.Common.Clear();
						foreach (KeyValuePair<string, string> commonParam in data.CommonParams)
						{
							skillDefinition.Common[commonParam.Key] = commonParam.Value;
						}
					}
					if (data.LevelParams != null && data.LevelParams.Count > 0)
					{
						skillDefinition.Levels = new Dictionary<int, Dictionary<string, string>>();
						foreach (KeyValuePair<int, Dictionary<string, string>> levelParam in data.LevelParams)
						{
							skillDefinition.Levels[levelParam.Key] = new Dictionary<string, string>(levelParam.Value);
						}
					}
					if (data.HLevels == null)
					{
						continue;
					}
					skillDefinition.HLevels.Clear();
					foreach (KeyValuePair<string, string> hLevel in data.HLevels)
					{
						skillDefinition.HLevels[hLevel.Key] = hLevel.Value;
					}
					ApplyConfigHints(skillDefinition);
					EnsureRecommendedRoute(skillDefinition, data);
					continue;
				}
				SkillDefinition skillDefinition2 = new SkillDefinition();
				skillDefinition2.SkillId = data.SkillId;
				skillDefinition2.Name = ((!string.IsNullOrEmpty(data.Name)) ? data.Name : data.SkillId.ToString());
				skillDefinition2.Desc = data.Desc ?? "";
				skillDefinition2.PDesc = data.PDesc ?? "";
				skillDefinition2.Ph = data.Ph ?? "";
				skillDefinition2.IconBase64 = data.IconBase64 ?? "";
				skillDefinition2.IconMouseOverBase64 = data.IconMouseOverBase64 ?? "";
				skillDefinition2.IconDisabledBase64 = data.IconDisabledBase64 ?? "";
				skillDefinition2.Action = data.Action ?? "";
				skillDefinition2.InfoType = data.InfoType;
				skillDefinition2.MaxLevel = ResolveMaxLevelFromWzData(data, 20);
				if (data.CommonParams != null)
				{
					foreach (KeyValuePair<string, string> commonParam2 in data.CommonParams)
					{
						skillDefinition2.Common[commonParam2.Key] = commonParam2.Value;
					}
				}
				if (data.LevelParams != null && data.LevelParams.Count > 0)
				{
					skillDefinition2.Levels = new Dictionary<int, Dictionary<string, string>>();
					foreach (KeyValuePair<int, Dictionary<string, string>> levelParam2 in data.LevelParams)
					{
						skillDefinition2.Levels[levelParam2.Key] = new Dictionary<string, string>(levelParam2.Value);
					}
				}
				if (data.HLevels != null)
				{
					foreach (KeyValuePair<string, string> hLevel2 in data.HLevels)
					{
						skillDefinition2.HLevels[hLevel2.Key] = hLevel2.Value;
					}
				}
				skillDefinition2.SourceLabel = "超级技能";
				skillDefinition2.ExistsInImg = true;
				ApplyConfigHints(skillDefinition2);
				EnsureRecommendedRoute(skillDefinition2, data);
				_pendingSkills.Add(skillDefinition2);
			}
			if (num > 0)
			{
				RefreshListView();
				Console.WriteLine($"[启动] 已从 .img 加载 {num} 个超级技能");
			}
		}
		catch (Exception ex)
		{
			Console.WriteLine("[启动] 扫描超级技能出错: " + ex.Message);
		}
	}
}


