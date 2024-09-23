#pragma once

#include "math.hpp"
#include "exactObjectList.hpp"
#include <string>

struct Worm;
struct Game;
struct Settings;
struct WormWeapon;

struct Weapon
{
	/*
	 * ShotType: (Projectile)
	  * Normal (0)
	  * Type1 (1)
	  * Steerable (2)
	  * Type2 (3)
	  * Laser (4)
	*/
	enum
	{
		STNormal,
		STDType1,
		STSteerable,
		STDType2,
		STLaser
	};

	void fire(Game& game, int angle, fixedvec vel, int speed, fixedvec pos, int ownerIdx, WormWeapon* ww) const;

	/*
	Additional worm detect distance for the bullet. Affects the distance at which an object hits a worm. Add more for "bigger" bullets or things like proximity detonators.
	Note: this parameter also determines starting distance from player (the distance at which the object is created).
	Note: if detectDistance < 0, then the worm will not receive damage from the object and also blowAway/wormCollide parameters will not work.
	*/
	int detectDistance;
	bool affectByWorm;
	/*
	Force affecting the worm on hit.
	Note: this will also work if the object has "wormCollide" set to false; in such case, it will simply not disappear and push the worm continuously.
	Note: works only if the object is moving and detectDistance ⩾ 0!
	*/
	int blowAway;
	/*
	Gravity of the projectile.
	*/
	fixed gravity;
	/*
	Whether the sObject should create a shadow.
	*/
	bool shadow;
	/*
	Whether a weapon has the flickering, red laser sight. It cannot be configured in any way, except for changing its colour (this requires palette changing though).
	Note: laser sight is not displayed on special rock (undefined) on the map (or after it), however the bullet itself passes through such type of material.
	*/
	bool laserSight;
	/*
	Sound to use when shooting (taken from soundpack). Set -1 for none.
	*/
	int launchSound;
	/*
	Sound to use when ??? (taken from soundpack). Set -1 for none.
	*/
	int loopSound;
	/*
	Sound to use when ??? (taken from soundpack). Set -1 for none.
	*/
	int exploSound;
	/*
	Initial speed of the bullet. 6 is about the max playable value for usual weapons.
	Note: if you set too high value for it, the bullet might pass through worms and even through thinner walls.
	Note: To make very fast gauss-like weapons, use "repeat" property.
	Note: if you set negative value for it, the bullet will go in the opposite direction the worm is aiming.

	Base speed used for missile-type weapons ("shotType" = 2). Use "addSpeed" property to define additional speed when pressing "up" button while flying.
	*/
	int speed;
	/*
	Works in two modes:
	a) for shotType = 3, this is additional speed added each frame. Use it for constant accelerating weapons.
	b) for shotType = 2 (directional player-controlled missile), this is an additional speed added to the missile when pressing up.
	It has no impact on other shotType (0, 1 and 4).
	*/
	fixed addSpeed;
	/*
	Spread of the weapon. This works by adding a random direction vector of random length to current speed vector of the projectile.
	Note: if you set its value to > 1 or < -1, then more than 1 direction vector will be applied, so that the projectiles will have a variable initial velocity after firing.

	weapon: This parameter doesn't do anything - only the wObject distribution property matters for bullets. Probably a bug.
	*/
	int distribution;
	/*
	Defines how many objects (particles) the weapon shoots. 1 for pistols, bazookas etc., 20 will be a shotgun-type weapon.
	*/
	int parts;
	/*
	Pushback force with which the worm is thrown away when firing a weapon.
	*/
	int recoil;
	/*
	Speed multiplication each frame. Use it to have a weapons which accelerate or decelerate non-linearly, like proxy mine from promode.
	*/
	int multSpeed;
	/*
	Delay time (in frames) between individual shots of the same weapon.
	*/
	int delay;
	/*
	Reload time.
	*/
	int loadingTime;
	/*
	Number of shots before weapon needs to be reloaded.
	*/
	int ammo;
	/*
	Which special object (sObject) to use on explosion. Set -1 for none.
	*/
	int createOnExp;
	/*
	Which dirt mask to use on object explosion. Set -1 is none.
	*/
	int dirtEffect;
	/*
	Frequency of shells ejected. Set to 0,1,2,3 or 4. 0=never, 1=always, 2=sometimes, 3=rarely, 4=very rarely.
	*/
	int leaveShells;
	/*
	Time between shot and shell ejected.
	*/
	int leaveShellDelay;
	/*
	Play reload sound when weapon is reloaded or not.
	*/
	bool playReloadSound;
	/*
	Whether the object should explode (produce a sObject and a sound indicated in exploSound) on worm collision.
	Note: works only if "wormCollide" is set "true" too!
	*/
	bool wormExplode;
	/*
	Whether the object should explode (produce a sObject and a sound indicated in exploSound) on ground collision.
	Note: works only if "bounce" parameter equals 0.
	*/
	bool explGround;
	/*
	Whether the object should collide with the worm and get removed.
	*/
	bool wormCollide;
	/*
	Duration of firecone sprite being displayed. It is taken from the sprite sheet, is not animated and always uses same sprites (9-15, depending on crosshair position).
	*/
	int fireCone;
	/*
	Whether the object should collide with other objects. If yes, they will bounce off themselves but none of them will be destroyed. If set to false, they pass through each other.
	Note: this property doesn't work if wObject "blowAway" parameter equals 0!
	Note: this property works also if "detectDistance" parameter of the wObject is < 0.
	*/
	bool collideWithObjects;
	/*
	Whether the object is affected by explosions' push force (on collision with sObject).
	Note: works only if the colliding sObject has got detectRange > 0 and damage ≠ 0 and blowAway ≠ 0!
	*/
	bool affectByExplosions;
	/*
	Speed multiply on hitting rock/dirt obstacle or the edge of the map. After every bounce, the projectile will get this percentage of its original speed.
	Note: if you set it to -1, then the bullet will pass through rock / dirt (this "wallhack" feature works only for wObject, it doesn't work for nObject).
	*/
	int bounce;
	/*
	Time to explode in frames. When set to 0 there will be no explosion at all.
	Any positive value will cause creation of a designated sObject indicated in createOnExp parameter (if not -1) and playing explosion sound indicated in exploSound (if not -1).
	Note: this parameter is affected by "repeat", which means that the higher the "repeat" value is set, the duration time before the object explodes will be proportionally shorter.
	Note: it is not recommended to set negative value for this property.
	Note: it is not recommended to set timeToExplo > 32767.
	*/
	int timeToExplo;
	/*
	Maximum (negative) variation of time to explode in frames.
	*/
	int timeToExploV;
	/*
	Damage inflicted on worm which was hit.
	Note: If the object has "wormCollide" property set to "false", this will be applied each frame the collision still occurs, leading to potentially huge damage values.
	Note: if you set negative value for it, you will have healing effect.
	*/
	int hitDamage;
	/*
	Determines how many blood particles (nObject6) should be created on worm hit, divided by 2.
	So, if you set it to "10", then 5 blood particles will be created on worm hit (per each frame - which means that the amount of blood particles is affected by "wormCollide" parameter).
	Note: this property works also if wObject "hitDamage" parameter equals 0.
	*/
	int bloodOnHit;
	/*
	First sprite of animation used for wObject.
	If -1, it will be a single pixel using a colour indicated in "colorBullets" parameter.
	Note: if you set -1 for this property and set "shotType": 2, then the bullet will be a single pixel but its colour will be changing depending on the object position (the direction the object is moving).
	*/
	int startFrame;
	/*
	Amount of sprites to use to animate the object, starting with "startFrame".
	Note: Animation begins on random frame, so it is suitable really only for objects which have animation cycle which looks good regardless of what frame it starts. Think things like spinning grenades, mines, pulsing items, etc.
	Note: works properly only for shotType 0, 1 and 4 (it's recommended to set this parameter to 0 for shotType 2 and 3).
	Note: the animation cycle is affected by "repeat" property, which means that the higher the "repeat" value is set, the faster the animation speed (the speed at which sprites change) will be (the delay before advancing to next frame will be lower).
	*/
	int numFrames;
	/*
	Whether the animation should be looped.
	Note: loopAnim parameter is affected by bulletSpeed parameter, which means that if you set it "true", then the animation cycle of the wObject will work only if the object is moving (when wObject stops moving, the animation cycle stops too).
	Note: if loopAnim is set to "false" and numFrames > 0, then the object will be still animated; in that case, the animation cycle of the wObject will work also if the object is not moving (the animation cycle keeps going even when the bullet is not moving) - unless the bullet stops moving on collision with ground (dirt / rock / edge of the map; in that case, the animation cycle always stops).
	Note: if loopAnim is set to "true" and numFrames: 0 and shotType: 0, then the wObject will have randomly either the sprite indicated in startFrame or the next one in spritesheet (e.g. if you set startFrame 210, then the wObject will appear as sprite 210 or as 211; this is actually how booby trap shoots weapon packs or health packs by default).
	Note: works properly only for shotType 0, 1 and 4 (it's recommended to set this parameter to "false" for shotType 2 and 3).
	*/
	bool loopAnim;
	/*
	Which bullet type (wObject) to use.
	Bullets (wObjects) are stored in an ordered array in "wObjects" section (counting is started from 0).
	So, if you have e.g. a chaingun bullet stored in third position, you need to set "bulletType: 2" here.
	Defines general type of the weapon object.
	0 - a standard object being either a colored pixel or animated sprite.
	1 - a missile-type object which uses different frames in the animation depending on its direction (when the bullet is turned in different angles), but only if "numFrames" paramterer is set to 0; in that case,"startFrame" defines the start of directional sprite range in the spritesheet (full sprite range is 13 sprites including the one indicated in "startFrame" parameter). In this shotType, wObject is not affected by addSpeed parameter.
	2 - a player-controllable missile. It is animated like shotType: 1 (however, full sprite range is 16 sprites, including the one indicated in "startFrame" parameter, but only if "numFrames" paramterer is set to 0).
	3 - a missile-type object with "drunk" behavior when "distribution" is set to non-zero value. It is animated like shotType: 1 (full sprite range is 13 sprites including the one indicated in "startFrame" parameter, but only if "numFrames" paramterer is set to 0). In this shotType, wObject is affected by addSpeed parameter.
	4 - in original Liero this was a "Laser-type" weapon (the bullets were very fast; to achieve this effect, shotType: 4 was affected by "repeat" parameter and this was hardcoded, i.e. if shotType was set to 4, then wObject 28 had "repeat" set to 1000, and other wObjects to 8).
	*/
	int shotType;
	/*
	Color of the object. Works only if u use a pixel (startFrame: -1) for a wObject.
	Note: this parameter also affects the colour of a laser beam created with "laserBeam" parameter.
	*/
	int colorBullets;
	/*
	Amount of nObjects to create on explosion. The wObject must actually explode, for example if "wormExplode" is set to false and "wormCollide" is set to true, no nObjects will be created.
	*/
	int splinterAmount;
	/*
	Color used on nObjects (produced as splinters) when they are a single pixel (startFrame -1 or 0). If splinterColour is set to 0 or 1, then splinters will have a colour indicated in nObject "colorBullets" property.
	Note: if splinterColour is set to 2 or more, nObject splinter will actually use two colours: the one indicated in this parameter, and also the previous one. So, in this case, splinters will use colour 13 and 12.
	*/
	int splinterColour;
	/*
	Type of nObjects used when an explosion occurs. This refers to index of the nObject in the array (counting started from 0), so if you change the order of nObjects, something else will be used.
	*/
	int splinterType;
	/*
	Way in which the splinter (nObject) is scattered when the wObject explodes:
	0 = all directions (like in big nuke);
	1 = direction the wObject is moving (like in mini nuke).
	*/
	int splinterScatter;
	/*
	sObject (special Object) used as a trail. Set -1 for none.
	Note: wObject keeps creating sObjects on its trail even when it stops moving (and even when it's spawned or "trapped" inside rock or dirt on the map).
	*/
	int objTrailType;
	/*
	Delay time (in frames) between creating trailing sObjects.
	Note: the delay is not referred to object's lifetime but game ticks, which means that it is measured in relation to the time elapsed since the game started, not since the object was created.
	Note: If "repeat" is set to greater than 1, sObjects will be created in "batches", creating intermittent lines instead of denser lines. This is probably a bug or unintended behavior.
	*/
	int objTrailDelay;
	/*
	This is NOT a type of object used for trailing something.
	"partTrail" here refers to nObject trail, however partTrailType defines how will it be dropped:
	0 - crackler-type non-directional trail (objects are dropped in all directions); in this case, the speed of nObject is affected by "SplinterCracklerVelDiv" parameter (section "constants");
	1 - larpa-type directional trail (objects are dropped in the direction the wObject is moving); in this case, the speed of nObject is affected by "SplinterLarpaVelDiv" parameter (section "constants").
	*/
	int partTrailType;
	/*
	This is the type of nObject trailed by the wObject. Set -1 for none.
	So, if you have e.g. smoke nObject stored in third position in the nObjects array and you want your wObject to create this smoke nObject as a trail, you need to set "2" here.
	Note: wObject keeps creating nObjects on its trail even when it stops moving (and even when it's spawned or "trapped" inside rock or dirt on the map).
	*/
	int partTrailObj;
	/*
	Delay time (in frames) between creating trailed nObjects.
	Note: the delay is not referred to object's lifetime but game ticks, which means that it is measured in relation to the time elapsed since the game started, not since the object was created.
	*/
	int partTrailDelay;
	bool chainExplosion;

	int computedLoadingTime(Settings& settings) const;

	int id;
	std::string name;
	std::string idStr;
};

struct WObject : ExactObjectListBase
{
	void blowUpObject(Game& game, int causeIdx);
	void process(Game& game);

	fixedvec pos, vel;
	//int id;
	Weapon const* type;
	int ownerIdx;
	int curFrame;
	int timeLeft;

	// STATS
	WormWeapon* firedBy;
	bool hasHit;
};
