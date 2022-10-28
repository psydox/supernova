//
// (c) 2022 Eduardo Doria.
//

#include "Audio.h"
#include "subsystem/AudioSystem.h"


using namespace Supernova;

Audio::Audio(Scene* scene): Object(scene){
    addComponent<AudioComponent>({});
}

Audio::~Audio(){

}

int Audio::loadAudio(std::string filename){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.filename = filename;

    return scene->getSystem<AudioSystem>()->loadAudio(audio, entity);
}

void Audio::destroyAudio(){
    AudioComponent& audio = getComponent<AudioComponent>();

    scene->getSystem<AudioSystem>()->destroyAudio(audio);
}

void Audio::play(){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.startTrigger = true;
}

void Audio::pause(){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.pauseTrigger = true;
}

void Audio::stop(){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.stopTrigger = true;
}

void Audio::seek(double time){
    AudioComponent& audio = getComponent<AudioComponent>();

    scene->getSystem<AudioSystem>()->seekAudio(audio, time);
}

double Audio::getLength(){
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.length;
}

double Audio::getPlayingTime(){
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.playingTime;
}

bool Audio::isPlaying(){
    AudioComponent& audio = getComponent<AudioComponent>();

    if ((audio.length > audio.playingTime) && (audio.playingTime != 0) && (audio.state == AudioState::Playing)) {
        return true;
    }
    return false;
}

bool Audio::isPaused(){
    AudioComponent& audio = getComponent<AudioComponent>();

    return (audio.state == AudioState::Paused);
}

bool Audio::isStopped(){
    AudioComponent& audio = getComponent<AudioComponent>();

    return (audio.state == AudioState::Stopped);
}

void Audio::set3DSound(bool enable3D){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.enable3D = enable3D;
}

bool Audio::is3DSound() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.enable3D;
}

void Audio::setClockedSound(bool enableClocked){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.enableClocked = enableClocked;
}

bool Audio::isClockedSound() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.enableClocked;
}

void Audio::setVolume(double volume){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.volume = volume;
    audio.needUpdate = true;
}

double Audio::getVolume() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.volume;
}

void Audio::setSpeed(float speed){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.speed = speed;
    audio.needUpdate = true;
}

float Audio::getSpeed() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.speed;
}

void Audio::setPan(float pan){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.pan = pan;
    audio.needUpdate = true;
}

float Audio::getPan() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.pan;
}

void Audio::setLopping(bool lopping){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.looping = lopping;
    audio.needUpdate = true;
}

bool Audio::isLopping() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.looping;
}

void Audio::setLoopingPoint(double loopingPoint){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.loopingPoint = loopingPoint;
    audio.needUpdate = true;
}

double Audio::getLoopingPoint() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.loopingPoint;
}

void Audio::setProtectVoice(bool protectVoice){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.protectVoice = protectVoice;
    audio.needUpdate = true;
}

bool Audio::isProtectVoice() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.protectVoice;
}

void Audio::setInaudibleBehavior(bool mustTick, bool kill){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.inaudibleBehaviorMustTick = mustTick;
    audio.inaudibleBehaviorKill = kill;
    audio.needUpdate = true;
}

void Audio::setMinMaxDistance(float minDistance, float maxDistance){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.minDistance = minDistance;
    audio.maxDistance = maxDistance;
    audio.needUpdate = true;
}

void Audio::setMinDistance(float minDistance){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.minDistance = minDistance;
    audio.needUpdate = true;
}

float Audio::getMinDistance() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.minDistance;
}

void Audio::setMaxDistance(float maxDistance){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.maxDistance = maxDistance;
    audio.needUpdate = true;
}

float Audio::getMaxDistance() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.maxDistance;
}

void Audio::setAttenuationModel(Audio3DAttenuation attenuationModel){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.attenuationModel = attenuationModel;
    audio.needUpdate = true;
}

Audio3DAttenuation Audio::getAttenuationModel() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.attenuationModel;
}

void Audio::setAttenuationRolloffFactor(float attenuationRolloffFactor){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.attenuationRolloffFactor = attenuationRolloffFactor;
    audio.needUpdate = true;
}

float Audio::getAttenuationRolloffFactor() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.attenuationRolloffFactor;
}

void Audio::setDopplerFactor(float dopplerFactor){
    AudioComponent& audio = getComponent<AudioComponent>();

    audio.dopplerFactor = dopplerFactor;
    audio.needUpdate = true;
}

float Audio::getDopplerFactor() const{
    AudioComponent& audio = getComponent<AudioComponent>();

    return audio.dopplerFactor;
}