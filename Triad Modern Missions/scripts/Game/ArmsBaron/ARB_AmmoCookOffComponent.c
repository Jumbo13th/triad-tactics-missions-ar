//! Детонация штабеля боеприпасов: когда объект с этим компонентом уничтожен, на его месте проходит серия
//! взрывов — склад рвётся, а не просто рассыпается.
//!
//! Серия идёт через глобальную очередь вызовов, а не через сам штабель: разрушаемый объект после последней
//! фазы удаляется, и вызовы на его компоненте оборвались бы на первом же взрыве.
//!
//! Компонент висит на наших префабах штабелей (`Prefabs/Props/ArmsBaron/ARB_AmmoStack_*.et`), поэтому ванильные
//! ящики и чужие миссии он не трогает.

class ARB_CookOff
{
	//------------------------------------------------------------------------------------------------
	//! Один взрыв серии и, если остались, заказ следующего. Статический: штабеля к этому моменту уже нет.
	//! В очередь вызовов идут только простые значения: BaseWorld она не принимает вовсе, а список взрывов
	//! передаётся строкой через "|" — так не зависим от того, какие ссылочные типы очередь пропускает.
	static void Burst(vector centre, string names, int left, float radius, float height, int intervalMs)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world || left <= 0 || names == "")
			return;

		array<string> explosions = {};
		names.Split("|", explosions, true);
		if (explosions.IsEmpty())
			return;

		ResourceName name = explosions[(explosions.Count() - left) % explosions.Count()];
		Resource prefab = Resource.Load(name);
		if (prefab && prefab.IsValid())
		{
			vector spot = centre;
			spot[0] = spot[0] + Math.RandomFloat(-radius, radius);
			spot[2] = spot[2] + Math.RandomFloat(-radius, radius);
			spot[1] = spot[1] + Math.RandomFloat(0, height);

			EntitySpawnParams params = new EntitySpawnParams();
			params.TransformMode = ETransformMode.WORLD;
			params.Transform[3] = spot;

			IEntity blast = GetGame().SpawnEntityPrefab(prefab, world, params);
			if (blast)
			{
				BaseTriggerComponent trigger = BaseTriggerComponent.Cast(blast.FindComponent(BaseTriggerComponent));
				if (trigger)
					trigger.OnUserTrigger(blast);          // так же запускает взрывы SCR_ExplosionGenerator
			}
		}

		if (left > 1)
			GetGame().GetCallqueue().CallLater(ARB_CookOff.Burst, intervalMs, false, centre, names,
				left - 1, radius, height, intervalMs);
	}
}

[ComponentEditorProps(category: "Arms Baron", description: "Серия взрывов, когда штабель боеприпасов уничтожен")]
class ARB_AmmoCookOffComponentClass : ScriptComponentClass
{
}

class ARB_AmmoCookOffComponent : ScriptComponent
{
	[Attribute("", UIWidgets.ResourceAssignArray,
		"Взрывы серии, по очереди. Ванильные заряды из Prefabs/Weapons/Warheads/Explosions.", "et",
		category: "Arms Baron")]
	protected ref array<ResourceName> m_aExplosions;

	[Attribute("6", UIWidgets.EditBox, "Сколько взрывов в серии", category: "Arms Baron")]
	protected int m_iBursts;

	[Attribute("7", UIWidgets.EditBox, "Разброс взрывов вокруг штабеля, м", category: "Arms Baron")]
	protected float m_fRadius;

	[Attribute("8", UIWidgets.EditBox, "Разброс взрывов вверх, м", category: "Arms Baron")]
	protected float m_fHeight;

	[Attribute("300", UIWidgets.EditBox, "Пауза между взрывами, мс", category: "Arms Baron")]
	protected int m_iIntervalMs;

	protected SCR_DamageManagerComponent m_Damage;
	protected bool m_bFired;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		if (!GetGame().InPlayMode())
			return;

		SetEventMask(owner, EntityEvent.INIT);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		if (!Replication.IsServer())
			return;

		m_Damage = SCR_DamageManagerComponent.GetDamageManager(owner);
		if (!m_Damage)
		{
			Print("ARB_AmmoCookOffComponent: у штабеля нет SCR_DamageManagerComponent, детонации не будет",
				LogLevel.WARNING);
			return;
		}

		if (m_Damage.IsDestroyed())                        // штабель уже разбит на старте миссии
			return;

		m_Damage.GetOnDamageStateChanged().Insert(OnDamageState);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnDamageState(EDamageState state)
	{
		if (m_bFired || state != EDamageState.DESTROYED)
			return;

		m_bFired = true;
		IEntity owner = GetOwner();
		if (!owner || !m_aExplosions || m_aExplosions.IsEmpty() || m_iBursts <= 0)
			return;

		// список уходит строкой: очередь вызовов переживёт сам штабель, а его компонент — нет
		string names;
		foreach (int i, ResourceName name : m_aExplosions)
		{
			if (i > 0)
				names += "|";

			names += name;
		}

		ARB_CookOff.Burst(owner.GetOrigin(), names, m_iBursts, m_fRadius, m_fHeight, m_iIntervalMs);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		if (m_Damage)
			m_Damage.GetOnDamageStateChanged().Remove(OnDamageState);
	}
}
