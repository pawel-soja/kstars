/*  Ekos state machine for a single capture job sequence
    SPDX-FileCopyrightText: Wolfgang Reissenberger <sterne-jaeger@openfuture.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "sequencejobstate.h"

#include "Options.h"
#include "kstarsdata.h"
#include "indicom.h"

namespace Ekos
{
SequenceJobState::SequenceJobState(const QSharedPointer<CaptureModuleState> &sharedState)
{
    m_CaptureModuleState = sharedState;
}

void SequenceJobState::setFrameType(CCDFrameType frameType)
{
    // set the frame type
    m_frameType = frameType;
    // reset the preparation state
    m_PreparationState = PREP_NONE;
}

void SequenceJobState::prepareLightFrameCapture(bool enforceCCDTemp, bool enforceInitialGuidingDrift, bool isPreview)
{
    // precondition: do not start while already being busy and conditions haven't changed
    if (m_status == JOB_BUSY && enforceCCDTemp == m_enforceTemperature && enforceInitialGuidingDrift == m_enforceInitialGuiding)
        return;

    m_status    = JOB_BUSY;
    m_isPreview = isPreview;

    // Reset all prepare actions
    setAllActionsReady();

    // disable batch mode for previews
    emit setCCDBatchMode(!isPreview);

    // Filter changes are actually done in capture(), therefore prepareActions are always true
    prepareActions[CaptureModuleState::ACTION_FILTER] = true;
    // nevertheless, emit an event so that Capture changes m_state
    if (targetFilterID != -1 && targetFilterID != m_CaptureModuleState->currentFilterID)
        emit prepareState(CAPTURE_CHANGING_FILTER);


    // Check if we need to update temperature (only skip if the value is initialized and within the limits)
    prepareTemperatureCheck(enforceCCDTemp);

    // Check if we need to update rotator (only skip if the value is initialized and within the limits)
    prepareRotatorCheck();

    // Check if we need to wait for guiding being initially below the target value
    m_enforceInitialGuiding = enforceInitialGuidingDrift;
    if (enforceInitialGuidingDrift && !isPreview)
        prepareActions[CaptureModuleState::ACTION_GUIDER_DRIFT] = false;

    // Hint: Filter changes are actually done in SequenceJob::capture();

    // preparation started
    m_PreparationState = PREP_BUSY;
    // check if the preparations are already completed
    checkAllActionsReady();
}

void SequenceJobState::prepareFlatFrameCapture(bool enforceCCDTemp, bool isPreview)
{
    // precondition: do not start while already being busy and conditions haven't changed
    if (m_status == JOB_BUSY && enforceCCDTemp == m_enforceTemperature)
        return;

    m_status    = JOB_BUSY;
    m_isPreview = isPreview;

    // Reset all prepare actions
    setAllActionsReady();

    // disable batch mode for previews
    emit setCCDBatchMode(!isPreview);

    // Filter changes are actually done in capture(), therefore prepareActions are always true
    prepareActions[CaptureModuleState::ACTION_FILTER] = true;
    // nevertheless, emit an event so that Capture changes m_state
    if (targetFilterID != -1 && targetFilterID != m_CaptureModuleState->currentFilterID)
        emit prepareState(CAPTURE_CHANGING_FILTER);

    // Check if we need to update temperature (only skip if the value is initialized and within the limits)
    prepareTemperatureCheck(enforceCCDTemp);

    // preparation started
    m_PreparationState = PREP_BUSY;
    // check if the preparations are already completed
    checkAllActionsReady();
}

void SequenceJobState::prepareDarkFrameCapture(bool enforceCCDTemp, bool isPreview)
{
    // precondition: do not start while already being busy and conditions haven't changed
    if (m_status == JOB_BUSY && enforceCCDTemp == m_enforceTemperature)
        return;

    m_status    = JOB_BUSY;
    m_isPreview = isPreview;

    // Reset all prepare actions
    setAllActionsReady();

    // disable batch mode for previews
    emit setCCDBatchMode(!isPreview);

    // Filter changes are actually done in capture(), therefore prepareActions are always true
    prepareActions[CaptureModuleState::ACTION_FILTER] = true;

    // Check if we need to update temperature (only skip if the value is initialized and within the limits)
    prepareTemperatureCheck(enforceCCDTemp);

    // preparation started
    m_PreparationState = PREP_BUSY;
    // check if the preparations are already completed
    checkAllActionsReady();
}

void SequenceJobState::prepareBiasFrameCapture(bool enforceCCDTemp, bool isPreview)
{
    prepareDarkFrameCapture(enforceCCDTemp, isPreview);
}

bool SequenceJobState::areActionsReady()
{
    for (bool &ready : prepareActions.values())
    {
        if (ready == false)
            return false;
    }

    return true;
}

void SequenceJobState::checkAllActionsReady()
{
    // do nothing if preparation is not running
    if (m_PreparationState != PREP_BUSY)
        return;

    switch (m_frameType)
    {
        case FRAME_LIGHT:
            if (areActionsReady())
            {
                // as last step ensure that the scope is uncovered
                if (checkLightFrameScopeCoverOpen() != IPS_OK)
                    return;

                m_PreparationState = PREP_COMPLETED;
                emit prepareComplete();
            }
            break;
        case FRAME_FLAT:
            if (!areActionsReady())
                return;

            // 1. Check if the selected flats light source is ready
            if (checkFlatsLightSourceReady() != IPS_OK)
                return;

            // 2. Light source ready, now check if we need to perform mount prepark
            if (checkPreMountParkReady() != IPS_OK)
                return;

            // 3. Check if we need to perform dome prepark
            if (checkPreDomeParkReady() != IPS_OK)
                return;

            // 4. If we used AUTOFOCUS before for a specific frame (e.g. Lum)
            //    then the absolute focus position for Lum is recorded in the filter manager
            //    when we take flats again, we always go back to the same focus position as the light frames to ensure
            //    near identical focus for both frames.
            if (checkFlatSyncFocus() != IPS_OK)
                return;

            // all preparations ready, avoid doubled events
            if (m_PreparationState == PREP_BUSY)
            {
                m_PreparationState = PREP_COMPLETED;
                emit prepareComplete();
            }
            break;
        case FRAME_DARK:
            if (!areActionsReady())
                return;

            // 1. check if the CCD has a shutter
            if (checkHasShutter() != IPS_OK)
                return;
            switch (flatFieldSource)
            {
                // All these are manual when it comes to dark frames
                case SOURCE_MANUAL:
                case SOURCE_DAWN_DUSK:
                    // For cameras without a shutter, we need to ask the user to cover the telescope
                    // if the telescope is not already covered.
                    if (checkManualCover() != IPS_OK)
                        return;
                    break;
                case SOURCE_FLATCAP:
                case SOURCE_DARKCAP:
                    if (checkDustCapReady(FRAME_DARK) != IPS_OK)
                        return;
                    break;

                case SOURCE_WALL:
                    if (checkWallPositionReady(FRAME_DARK) != IPS_OK)
                        return;
                    break;
            }

            // avoid doubled events
            if (m_PreparationState == PREP_BUSY)
            {
                m_PreparationState = PREP_COMPLETED;
                emit prepareComplete();
            }
            break;
        case FRAME_BIAS:
            if (areActionsReady())
            {
                // avoid doubled events
                if (m_PreparationState == PREP_BUSY)
                {
                    m_PreparationState = PREP_COMPLETED;
                    emit prepareComplete();
                }
            }
            break;
        default:
            // all other cases not refactored yet, preparation immediately completed
            emit prepareComplete();
            break;
    }
}

void SequenceJobState::setAllActionsReady()
{
    QMutableMapIterator<CaptureModuleState::PrepareActions, bool> it(prepareActions);

    while (it.hasNext())
    {
        it.next();
        it.setValue(true);
    }
    // reset the initialization state
    for (CaptureModuleState::PrepareActions action :
            {
                CaptureModuleState::ACTION_FILTER, CaptureModuleState::ACTION_ROTATOR, CaptureModuleState::ACTION_TEMPERATURE,
                CaptureModuleState::ACTION_GUIDER_DRIFT
            })
        setInitialized(action, false);
}

void SequenceJobState::prepareTemperatureCheck(bool enforceCCDTemp)
{
    // turn on CCD temperature enforcing if required
    m_enforceTemperature = enforceCCDTemp;

    if (m_enforceTemperature)
    {
        prepareActions[CaptureModuleState::ACTION_TEMPERATURE] = false;
        if (isInitialized(CaptureModuleState::ACTION_TEMPERATURE))
        {
            // ignore the next value since after setting temperature the next received value will be
            // exactly this value no matter what the CCD temperature
            ignoreNextValue[CaptureModuleState::ACTION_TEMPERATURE] = true;
            // request setting temperature
            emit setCCDTemperature(targetTemperature);
            emit prepareState(CAPTURE_SETTING_TEMPERATURE);
        }
        // trigger setting current value first if not initialized
        else
            emit readCurrentState(CAPTURE_SETTING_TEMPERATURE);

    }
}

void SequenceJobState::prepareRotatorCheck()
{
    if (targetPositionAngle > Ekos::INVALID_VALUE)
    {
        if (isInitialized(CaptureModuleState::ACTION_ROTATOR))
        {
            prepareActions[CaptureModuleState::ACTION_ROTATOR] = false;
            // RawAngle = PA + Offset / Multiplier -> see Capture::Capture()
            double rawAngle = (targetPositionAngle + Options::pAOffset()) / Options::pAMultiplier();
            emit prepareState(CAPTURE_SETTING_ROTATOR);
            emit setRotatorAngle(&rawAngle);
        }
        // trigger setting current value first if not initialized
        else
            emit readCurrentState(CAPTURE_SETTING_ROTATOR);
    }
}

IPState SequenceJobState::checkFlatsLightSourceReady()
{
    IPState result = IPS_OK;

    switch (flatFieldSource)
    {
        case SOURCE_MANUAL:
            result = checkManualFlatsCoverReady();
            break;
        case SOURCE_DAWN_DUSK:
            // Not implemented.
            result = IPS_ALERT;
            break;
        case SOURCE_FLATCAP:
            result = checkFlatCapReady();
            break;
        case SOURCE_WALL:
            result = checkWallPositionReady(FRAME_FLAT);
            break;
        case SOURCE_DARKCAP:
            result = checkDustCapReady(FRAME_FLAT);
            break;
    }
    return result;
}

IPState SequenceJobState::checkManualFlatsCoverReady()
{
    // Manual mode we need to cover mount with evenly illuminated field.
    if (m_CaptureModuleState->telescopeCovered == false)
    {
        if (coverQueryState == CAL_CHECK_CONFIRMATION)
            return IPS_BUSY;

        // request asking the user to cover the scope manually with a light source
        emit askManualScopeLightCover(i18n("Cover telescope with an evenly illuminated light source."),
                                      i18n("Flat Frame"));
        coverQueryState = CAL_CHECK_CONFIRMATION;

        return IPS_BUSY;
    }
    return IPS_OK;
}

IPState SequenceJobState::checkFlatCapReady()
{
    // flat light is on
    if (m_CaptureModuleState->getLightBoxLightState() == CaptureModuleState::CAP_LIGHT_ON)
        return IPS_OK;
    // turning on flat light running
    if (m_CaptureModuleState->getLightBoxLightState() == CaptureModuleState::CAP_LIGHT_BUSY ||
            m_CaptureModuleState->getDustCapState() == CaptureModuleState::CAP_PARKING)
        return IPS_BUSY;
    // error occured
    if (m_CaptureModuleState->getDustCapState() == CaptureModuleState::CAP_ERROR)
        return IPS_ALERT;

    // #1 if using the dust cap, first park the dust cap
    if (m_CaptureModuleState->hasDustCap && m_CaptureModuleState->getDustCapState() != CaptureModuleState::CAP_PARKED)
    {
        m_CaptureModuleState->setDustCapState(CaptureModuleState::CAP_PARKING);
        emit parkDustCap(true);
        emit newLog(i18n("Parking dust cap..."));
        return IPS_BUSY;
    }

    // #2 Then we check if we need to turn on light box, if any
    if (m_CaptureModuleState->hasLightBox && m_CaptureModuleState->getLightBoxLightState() != CaptureModuleState::CAP_LIGHT_ON)
    {
        m_CaptureModuleState->setLightBoxLightState(CaptureModuleState::CAP_LIGHT_BUSY);
        emit setLightBoxLight(true);
        emit newLog(i18n("Turn light box light on..."));
        return IPS_BUSY;
    }

    // nothing more to do
    return IPS_OK;
}

IPState SequenceJobState::checkDustCapReady(CCDFrameType frameType)
{
    // turning on flat light running
    if (m_CaptureModuleState->getLightBoxLightState() == CaptureModuleState::CAP_LIGHT_BUSY  ||
            m_CaptureModuleState->getDustCapState() == CaptureModuleState::CAP_PARKING ||
            m_CaptureModuleState->getDustCapState() == CaptureModuleState::CAP_UNPARKING)
        return IPS_BUSY;
    // error occured
    if (m_CaptureModuleState->getDustCapState() == CaptureModuleState::CAP_ERROR)
        return IPS_ALERT;

    bool captureFlats = (frameType == FRAME_FLAT);

    // for flats open the cap and close it otherwise
    CaptureModuleState::CapState targetCapState = captureFlats ? CaptureModuleState::CAP_IDLE : CaptureModuleState::CAP_PARKED;
    // If cap is parked, unpark it since dark cap uses external light source.
    if (m_CaptureModuleState->hasDustCap && m_CaptureModuleState->getDustCapState() != targetCapState)
    {
        m_CaptureModuleState->setDustCapState(captureFlats ? CaptureModuleState::CAP_UNPARKING : CaptureModuleState::CAP_PARKING);
        emit parkDustCap(!captureFlats);
        emit newLog(captureFlats ? i18n("Unparking dust cap...") : i18n("Parking dust cap..."));
        return IPS_BUSY;
    }

    CaptureModuleState::LightState targetLightBoxStatus = (frameType == FRAME_FLAT) ? CaptureModuleState::CAP_LIGHT_ON :
            CaptureModuleState::CAP_LIGHT_OFF;

    if (m_CaptureModuleState->hasLightBox && m_CaptureModuleState->getLightBoxLightState() != targetLightBoxStatus)
    {
        m_CaptureModuleState->setLightBoxLightState(CaptureModuleState::CAP_LIGHT_BUSY);
        emit setLightBoxLight(captureFlats);
        emit newLog(captureFlats ? i18n("Turn light box light on...") : i18n("Turn light box light off..."));
        return IPS_BUSY;
    }

    // nothing more to do
    return IPS_OK;
}

IPState SequenceJobState::checkWallPositionReady(CCDFrameType frametype)
{
    if (m_CaptureModuleState->hasTelescope)
    {
        if (wpScopeStatus < WP_SLEWING)
        {
            wallCoord.HorizontalToEquatorial(KStarsData::Instance()->lst(),
                                             KStarsData::Instance()->geo()->lat());
            wpScopeStatus = WP_SLEWING;
            emit slewTelescope(wallCoord);
            emit newLog(i18n("Mount slewing to wall position..."));
            return IPS_BUSY;
        }
        // wait until actions completed
        else if (wpScopeStatus == WP_SLEWING || wpScopeStatus == WP_TRACKING_BUSY)
            return IPS_BUSY;
        // Check if slewing is complete
        else if (wpScopeStatus == WP_SLEW_COMPLETED)
        {
            wpScopeStatus = WP_TRACKING_BUSY;
            emit setScopeTracking(false);
            emit newLog(i18n("Slew to wall position complete, stop tracking."));
            return IPS_BUSY;
        }
        else if (wpScopeStatus == WP_TRACKING_OFF)
            emit newLog(i18n("Slew to wall position complete, stop tracking."));

        // wall position reached, check if we have a light box to turn on for flats and off otherwise
        bool captureFlats = (frametype == FRAME_FLAT);
        CaptureModuleState::LightState targetLightState = (captureFlats ? CaptureModuleState::CAP_LIGHT_ON :
                CaptureModuleState::CAP_LIGHT_OFF);

        if (m_CaptureModuleState->hasLightBox == true)
        {
            if (m_CaptureModuleState->getLightBoxLightState() != targetLightState)
            {
                m_CaptureModuleState->setLightBoxLightState(CaptureModuleState::CAP_LIGHT_BUSY);
                emit setLightBoxLight(captureFlats);
                emit newLog(captureFlats ? i18n("Turn light box light on...") : i18n("Turn light box light off..."));
                return IPS_BUSY;
            }
        }
    }
    // everything ready
    return IPS_OK;
}

IPState SequenceJobState::checkPreMountParkReady()
{
    if (preMountPark && m_CaptureModuleState->hasTelescope && flatFieldSource != SOURCE_WALL)
    {
        if (m_CaptureModuleState->getScopeParkState() == ISD::PARK_ERROR)
        {
            emit newLog(i18n("Parking mount failed, aborting..."));
            emit abortCapture();
            return IPS_ALERT;
        }
        else if (m_CaptureModuleState->getScopeParkState() == ISD::PARK_PARKING)
            return IPS_BUSY;
        else if (m_CaptureModuleState->getScopeParkState() != ISD::PARK_PARKED)
        {
            m_CaptureModuleState->setScopeParkState(ISD::PARK_PARKING);
            emit setScopeParked(true);
            emit newLog(i18n("Parking mount prior to calibration frames capture..."));
            return IPS_BUSY;
        }
    }
    // everything ready
    return IPS_OK;
}

IPState SequenceJobState::checkPreDomeParkReady()
{
    if (preDomePark && m_CaptureModuleState->hasDome)
    {
        if (m_CaptureModuleState->getDomeState() == ISD::Dome::DOME_ERROR)
        {
            emit newLog(i18n("Parking dome failed, aborting..."));
            emit abortCapture();
            return IPS_ALERT;
        }
        else if (m_CaptureModuleState->getDomeState() == ISD::Dome::DOME_PARKING)
            return IPS_BUSY;
        else if (m_CaptureModuleState->getDomeState() != ISD::Dome::DOME_PARKED)
        {
            m_CaptureModuleState->setDomeState(ISD::Dome::DOME_PARKING);
            emit setDomeParked(true);
            emit newLog(i18n("Parking dome prior to calibration frames capture..."));
            return IPS_BUSY;
        }
    }
    // everything ready
    return IPS_OK;
}

IPState SequenceJobState::checkFlatSyncFocus()
{
    // check already running?
    if (flatSyncStatus == FS_BUSY)
    {
        QTimer::singleShot(1000, [&]
        {
            // wait for one second and repeat the request again
            emit flatSyncFocus(targetFilterID);
        });
        return IPS_BUSY;
    }

    if (m_frameType == FRAME_FLAT && autoFocusReady && Options::flatSyncFocus() &&
            flatSyncStatus != FS_COMPLETED)
    {
        flatSyncStatus = FS_BUSY;
        emit flatSyncFocus(targetFilterID);
        return IPS_BUSY;
    }
    // everything ready
    return IPS_OK;
}

IPState SequenceJobState::checkHasShutter()
{
    if (m_CaptureModuleState->shutterStatus == CaptureModuleState::SHUTTER_BUSY)
        return IPS_BUSY;
    if (m_CaptureModuleState->shutterStatus != CaptureModuleState::SHUTTER_UNKNOWN)
        return IPS_OK;
    // query the status
    m_CaptureModuleState->shutterStatus = CaptureModuleState::SHUTTER_BUSY;
    emit queryHasShutter();
    return IPS_BUSY;
}

IPState SequenceJobState::checkManualCover()
{
    if (m_CaptureModuleState->shutterStatus == CaptureModuleState::SHUTTER_NO
            && m_CaptureModuleState->telescopeCovered == false)
    {
        // Already asked for confirmation? Then wait.
        if (coverQueryState == CAL_CHECK_CONFIRMATION)
            return IPS_BUSY;

        // Otherwise, we ask user to confirm manually
        coverQueryState = CAL_CHECK_CONFIRMATION;

        emit askManualScopeLightCover(i18n("Cover the telescope in order to take a dark exposure."),
                                      i18n("Dark Exposure"));
        return IPS_BUSY;
    }
    // everything ready
    return IPS_OK;
}

IPState SequenceJobState::checkLightFrameScopeCoverOpen()
{
    switch (flatFieldSource)
    {
        // All these are considered MANUAL when it comes to light frames
        case SOURCE_MANUAL:
        case SOURCE_DAWN_DUSK:
        case SOURCE_WALL:
            // If telescopes were MANUALLY covered before
            // we need to manually uncover them.
            if (m_CaptureModuleState->telescopeCovered)
            {
                // If we already asked for confirmation and waiting for it
                // let us see if the confirmation is fulfilled
                // otherwise we return.
                if (coverQueryState == CAL_CHECK_CONFIRMATION)
                    return IPS_BUSY;

                emit askManualScopeLightOpen();

                return IPS_BUSY;
            }
            break;
        case SOURCE_FLATCAP:
        case SOURCE_DARKCAP:
            // if no state update happened, wait.
            if (m_CaptureModuleState->getLightBoxLightState() == CaptureModuleState::CAP_LIGHT_BUSY ||
                    m_CaptureModuleState->getDustCapState() == CaptureModuleState::CAP_UNPARKING)
                return IPS_BUSY;

            // Account for light box only (no dust cap)
            if (m_CaptureModuleState->hasLightBox && m_CaptureModuleState->getLightBoxLightState() != CaptureModuleState::CAP_LIGHT_OFF)
            {
                m_CaptureModuleState->setLightBoxLightState(CaptureModuleState::CAP_LIGHT_BUSY);
                emit setLightBoxLight(false);
                emit newLog(i18n("Turn light box light off..."));
                return IPS_BUSY;
            }

            if (m_CaptureModuleState->hasDustCap == false)
            {
                emit newLog("Skipping flat/dark cap since it is not connected.");
                return IPS_OK;
            }

            // If cap is parked, we need to unpark it
            if (m_CaptureModuleState->getDustCapState() != CaptureModuleState::CAP_IDLE)
            {
                m_CaptureModuleState->setDustCapState(CaptureModuleState::CAP_UNPARKING);
                emit parkDustCap(false);
                emit newLog(i18n("Unparking dust cap..."));
                return IPS_BUSY;
            }
            break;
    }
    // scope cover open (or no scope cover)
    return IPS_OK;
}

bool SequenceJobState::isInitialized(CaptureModuleState::PrepareActions action)
{
    return m_CaptureModuleState.data()->isInitialized[action];
}

void SequenceJobState::setInitialized(CaptureModuleState::PrepareActions action, bool init)
{
    m_CaptureModuleState.data()->isInitialized[action] = init;
}

void SequenceJobState::setCurrentFilterID(int value)
{
    m_CaptureModuleState->currentFilterID = value;
    setInitialized(CaptureModuleState::ACTION_FILTER, true);

    // TODO introduce settle time
    if (m_CaptureModuleState->currentFilterID == targetFilterID)
        prepareActions[CaptureModuleState::ACTION_FILTER] = true;

    checkAllActionsReady();
}

void SequenceJobState::setCurrentCCDTemperature(double currentTemperature)
{
    // skip if next value should be ignored
    if (ignoreNextValue[CaptureModuleState::ACTION_TEMPERATURE])
    {
        ignoreNextValue[CaptureModuleState::ACTION_TEMPERATURE] = false;
        return;
    }

    if (isInitialized(CaptureModuleState::ACTION_TEMPERATURE))
    {
        if (m_enforceTemperature == false
                || fabs(targetTemperature - currentTemperature) <= Options::maxTemperatureDiff())
            prepareActions[CaptureModuleState::ACTION_TEMPERATURE] = true;

        checkAllActionsReady();
    }
    else
    {
        setInitialized(CaptureModuleState::ACTION_TEMPERATURE, true);
        if (m_enforceTemperature == false
                || fabs(targetTemperature - currentTemperature) <= Options::maxTemperatureDiff())
        {
            prepareActions[CaptureModuleState::ACTION_TEMPERATURE] = true;
            checkAllActionsReady();
        }
        else
        {
            prepareTemperatureCheck(m_enforceTemperature);
        }
    }
}

void SequenceJobState::setCurrentRotatorPositionAngle(double rotatorAngle, IPState state)
{
    // PA = RawAngle * Multiplier - Offset -> see Capture::Capture()
    double currentPositionAngle = range360(rotatorAngle * Options::pAMultiplier() - Options::pAOffset());
    if (currentPositionAngle > 180)
        currentPositionAngle -= 360.0;

    if (isInitialized(CaptureModuleState::ACTION_ROTATOR))
    {
        // TODO introduce settle time
        // TODO make sure rotator has fully stopped
        if (fabs(currentPositionAngle - targetPositionAngle) * 60 <= Options::astrometryRotatorThreshold()
                && state != IPS_BUSY)
            prepareActions[CaptureModuleState::ACTION_ROTATOR] = true;

        checkAllActionsReady();
    }
    else
    {
        setInitialized(CaptureModuleState::ACTION_ROTATOR, true);
        if (fabs(currentPositionAngle - targetPositionAngle) * 60 <= Options::astrometryRotatorThreshold()
                && state != IPS_BUSY)
        {
            prepareActions[CaptureModuleState::ACTION_ROTATOR] = true;
            checkAllActionsReady();
        }
        else
        {
            prepareRotatorCheck();
        }
    }
}

void SequenceJobState::setCurrentGuiderDrift(double value)
{
    setInitialized(CaptureModuleState::ACTION_GUIDER_DRIFT, true);
    if (value <= targetStartGuiderDrift)
        prepareActions[CaptureModuleState::ACTION_GUIDER_DRIFT] = true;

    checkAllActionsReady();
}

void SequenceJobState::manualScopeLightCover(bool closed, bool success)
{
    // covering confirmed
    if (success == true)
    {
        m_CaptureModuleState->telescopeCovered = closed;
        coverQueryState = CAL_CHECK_TASK;
        // re-run checks
        checkAllActionsReady();
    }
    // cancelled
    else
    {
        m_CaptureModuleState->shutterStatus = CaptureModuleState::SHUTTER_UNKNOWN;
        coverQueryState = CAL_CHECK_TASK;
        // abort, no further checks
        emit abortCapture();
    }
}

void SequenceJobState::lightBoxLight(bool on)
{
    m_CaptureModuleState->setLightBoxLightState(on ? CaptureModuleState::CAP_LIGHT_ON : CaptureModuleState::CAP_LIGHT_OFF);
    emit newLog(i18n(on ? "Light box on." : "Light box off."));
    // re-run checks
    checkAllActionsReady();
}

void SequenceJobState::dustCapStateChanged(ISD::DustCap::Status status)
{
    switch (status)
    {
        case ISD::DustCap::CAP_ERROR:
            m_CaptureModuleState->setDustCapState(CaptureModuleState::CAP_ERROR);
            emit abortCapture();
            break;
        case ISD::DustCap::CAP_PARKED:
            m_CaptureModuleState->setDustCapState(CaptureModuleState::CAP_PARKED);
            emit newLog(i18n("Dust cap parked."));
            break;
        case ISD::DustCap::CAP_IDLE:
            m_CaptureModuleState->setDustCapState(CaptureModuleState::CAP_IDLE);
            emit newLog(i18n("Dust cap unparked."));
            break;
        case ISD::DustCap::CAP_UNPARKING:
            m_CaptureModuleState->setDustCapState(CaptureModuleState::CAP_UNPARKING);
            break;
        case ISD::DustCap::CAP_PARKING:
            m_CaptureModuleState->setDustCapState(CaptureModuleState::CAP_PARKING);
            break;
    }

    // re-run checks
    checkAllActionsReady();
}

void SequenceJobState::scopeStatusChanged(ISD::Mount::Status status)
{
    // handle wall position
    switch (status)
    {
        case ISD::Mount::MOUNT_TRACKING:
            if (wpScopeStatus == WP_SLEWING)
                wpScopeStatus = WP_SLEW_COMPLETED;
            break;
        case ISD::Mount::MOUNT_IDLE:
            if (wpScopeStatus == WP_SLEWING || wpScopeStatus == WP_TRACKING_BUSY)
                wpScopeStatus = WP_TRACKING_OFF;
            break;
        default:
            // do nothing
            break;
    }
    m_CaptureModuleState->setScopeState(status);
    // re-run checks
    checkAllActionsReady();
}

void SequenceJobState::scopeParkStatusChanged(ISD::ParkStatus status)
{
    m_CaptureModuleState->setScopeParkState(status);
    // re-run checks
    checkAllActionsReady();
}

void SequenceJobState::domeStatusChanged(ISD::Dome::Status status)
{
    m_CaptureModuleState->setDomeState(status);
    // re-run checks
    checkAllActionsReady();
}

void SequenceJobState::flatSyncFocusChanged(bool completed)
{
    flatSyncStatus = (completed ? FS_COMPLETED : FS_BUSY);
    // re-run checks
    checkAllActionsReady();
}

void SequenceJobState::hasShutter(bool present)
{
    if (present == true)
        m_CaptureModuleState->shutterStatus = CaptureModuleState::SHUTTER_YES;
    else
        m_CaptureModuleState->shutterStatus = CaptureModuleState::SHUTTER_NO;

    // re-run checks
    checkAllActionsReady();
}

void SequenceJobState::setEnforceInitialGuidingDrift(bool enforceInitialGuidingDrift)
{
    m_enforceInitialGuiding = enforceInitialGuidingDrift;
    // update the preparation action
    prepareActions[CaptureModuleState::ACTION_GUIDER_DRIFT] = !enforceInitialGuidingDrift || m_isPreview;
    // re-run checks
    checkAllActionsReady();
}

SequenceJobState::PreparationState SequenceJobState::getPreparationState() const
{
    return m_PreparationState;
}
} // namespace
