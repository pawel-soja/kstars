/*
    SPDX-FileCopyrightText: 2012 Jasem Mutlaq <mutlaqja@ikarustech.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "ekos/auxiliary/filtermanager.h"
#include "ui_focus.h"
#include "focusprofileplot.h"
#include "ekos/ekos.h"
#include "ekos/auxiliary/stellarsolverprofileeditor.h"
#include "ekos/auxiliary/darkprocessor.h"
#include "ekos/mount/mount.h"
#include "fitsviewer/fitsviewer.h"
#include "indi/indicamera.h"
#include "indi/indifocuser.h"
#include "indi/indistd.h"
#include "indi/indiweather.h"
#include "indi/indimount.h"

#include <QtDBus/QtDBus>
#include <parameters.h>

namespace Ekos
{

class FocusAlgorithmInterface;
class PolynomialFit;

/**
 * @class Focus
 * @short Supports manual focusing and auto focusing using relative and absolute INDI focusers.
 *
 * @author Jasem Mutlaq
 * @version 1.5
 */
class Focus : public QWidget, public Ui::Focus
{
        Q_OBJECT
        Q_CLASSINFO("D-Bus Interface", "org.kde.kstars.Ekos.Focus")
        Q_PROPERTY(Ekos::FocusState status READ status NOTIFY newStatus)
        Q_PROPERTY(QStringList logText READ logText NOTIFY newLog)
        Q_PROPERTY(QString opticalTrain READ opticalTrain WRITE setOpticalTrain)
        Q_PROPERTY(QString camera READ camera)
        Q_PROPERTY(QString focuser READ focuser)
        Q_PROPERTY(QString filterWheel READ filterWheel)
        Q_PROPERTY(QString filter READ filter WRITE setFilter)
        Q_PROPERTY(double HFR READ getHFR NOTIFY newHFR)
        Q_PROPERTY(double exposure READ exposure WRITE setExposure)

    public:
        Focus();
        ~Focus();

        typedef enum { FOCUS_NONE, FOCUS_IN, FOCUS_OUT } Direction;
        typedef enum { FOCUS_MANUAL, FOCUS_AUTO } Type;
        typedef enum { FOCUS_ITERATIVE, FOCUS_POLYNOMIAL, FOCUS_LINEAR, FOCUS_LINEAR1PASS } Algorithm;
        //typedef enum { FOCUSER_TEMPERATURE, OBSERVATORY_TEMPERATURE, NO_TEMPERATURE } TemperatureSource;

        /** @defgroup FocusDBusInterface Ekos DBus Interface - Focus Module
             * Ekos::Focus interface provides advanced scripting capabilities to perform manual and automatic focusing operations.
            */

        /*@{*/

        /** DBUS interface function.
             * select the CCD device from the available CCD drivers.
             * @param device The CCD device name
             * @return Returns true if CCD device is found and set, false otherwise.
             */
        Q_SCRIPTABLE QString camera();

        /** DBUS interface function.
             * select the focuser device from the available focuser drivers. The focuser device can be the same as the CCD driver if the focuser functionality was embedded within the driver.
             * @param device The focuser device name
             * @return Returns true if focuser device is found and set, false otherwise.
             */
        Q_SCRIPTABLE QString focuser();

        /** DBUS interface function.
             * select the filter device from the available filter drivers. The filter device can be the same as the CCD driver if the filter functionality was embedded within the driver.
             * @param device The filter device name
             * @return Returns true if filter device is found and set, false otherwise.
             */
        Q_SCRIPTABLE QString filterWheel();

        /** DBUS interface function.
             * select the filter from the available filters.
             * @param filter The filter name
             * @return Returns true if filter is found and set, false otherwise.
             */
        Q_SCRIPTABLE bool setFilter(const QString &filter);
        Q_SCRIPTABLE QString filter();

        /** DBUS interface function.
             * @return Returns True if current focuser supports auto-focusing
             */
        Q_SCRIPTABLE bool canAutoFocus()
        {
            return (m_FocusType == FOCUS_AUTO);
        }

        /** DBUS interface function.
             * @return Returns Half-Flux-Radius in pixels.
             */
        Q_SCRIPTABLE double getHFR()
        {
            return currentHFR;
        }

        /** DBUS interface function.
             * Set CCD exposure value
             * @param value exposure value in seconds.
             */
        Q_SCRIPTABLE Q_NOREPLY void setExposure(double value);
        Q_SCRIPTABLE double exposure()
        {
            return focusExposure->value();
        }

        /** DBUS interface function.
             * Set CCD binning
             * @param binX horizontal binning
             * @param binY vertical binning
             */
        Q_SCRIPTABLE Q_NOREPLY void setBinning(int binX, int binY);

        /** DBUS interface function.
             * Set Auto Focus options. The options must be set before starting the autofocus operation. If no options are set, the options loaded from the user configuration are used.
             * @param enable If true, Ekos will attempt to automatically select the best focus star in the frame. If it fails to select a star, the user will be asked to select a star manually.
             */
        Q_SCRIPTABLE Q_NOREPLY void setAutoStarEnabled(bool enable);

        /** DBUS interface function.
             * Set Auto Focus options. The options must be set before starting the autofocus operation. If no options are set, the options loaded from the user configuration are used.
             * @param enable if true, Ekos will capture a subframe around the selected focus star. The subframe size is determined by the boxSize parameter.
             */
        Q_SCRIPTABLE Q_NOREPLY void setAutoSubFrameEnabled(bool enable);

        /** DBUS interface function.
             * Set Autofocus parameters
             * @param boxSize the box size around the focus star in pixels. The boxsize is used to subframe around the focus star.
             * @param stepSize the initial step size to be commanded to the focuser. If the focuser is absolute, the step size is in ticks. For relative focusers, the focuser will be commanded to focus inward for stepSize milliseconds initially.
             * @param maxTravel the maximum steps permitted before the autofocus operation aborts.
             * @param tolerance Measure of how accurate the autofocus algorithm is. If the difference between the current HFR and minimum measured HFR is less than %tolerance after the focuser traversed both ends of the V-curve, then the focusing operation
             * is deemed successful. Otherwise, the focusing operation will continue.
             */
        Q_SCRIPTABLE Q_NOREPLY void setAutoFocusParameters(int boxSize, int stepSize, int maxTravel, double tolerance);

        /** DBUS interface function.
            * resetFrame Resets the CCD frame to its full native resolution.
            */
        Q_SCRIPTABLE Q_NOREPLY void resetFrame();

        /** DBUS interface function.
            * Return state of Focuser module (Ekos::FocusState)
            */

        Q_SCRIPTABLE Ekos::FocusState status()
        {
            return state;
        }

        /** @}*/

        /**
             * @brief Add CCD to the list of available CCD.
             * @param newCCD pointer to CCD device.
             * @return True if added successfully, false if duplicate or failed to add.
             */
        bool setCamera(ISD::Camera *device);

        /**
             * @brief addFocuser Add focuser to the list of available focusers.
             * @param newFocuser pointer to focuser device.
             * @return True if added successfully, false if duplicate or failed to add.
             */
        bool setFocuser(ISD::Focuser *device);

        /**
             * @brief addFilter Add filter to the list of available filters.
             * @param newFilter pointer to filter device.
             * @return True if added successfully, false if duplicate or failed to add.
             */
        bool setFilterWheel(ISD::FilterWheel *device);


        /**
             * @brief addTemperatureSource Add temperature source to the list of available sources.
             * @param newSource Device with temperature reporting capability
             * @return True if added successfully, false if duplicate or failed to add.
             */
        bool addTemperatureSource(const QSharedPointer<ISD::GenericDevice> &device);

        /**
         * @brief removeDevice Remove device from Focus module
         * @param deviceRemoved pointer to device
         */
        void removeDevice(const QSharedPointer<ISD::GenericDevice> &deviceRemoved);

        const QSharedPointer<FilterManager> &filterManager() const
        {
            return m_FilterManager;
        }
        void setupFilterManager();

        void clearLog();
        QStringList logText()
        {
            return m_LogText;
        }
        QString getLogText()
        {
            return m_LogText.join("\n");
        }

        // All
        QVariantMap getAllSettings() const;
        void setAllSettings(const QVariantMap &settings);

    public slots:

        /** \addtogroup FocusDBusInterface
             *  @{
             */

        /* Focus */
        /** DBUS interface function.
             * Start the autofocus operation.
             */
        Q_SCRIPTABLE Q_NOREPLY void start();

        /** DBUS interface function.
             * Abort the autofocus operation.
             */
        Q_SCRIPTABLE Q_NOREPLY void abort();

        /** DBUS interface function.
             * Capture a focus frame.
             * @param settleTime if > 0 wait for the given time in seconds before starting to capture
             */
        Q_SCRIPTABLE Q_NOREPLY void capture(double settleTime = 0.0);

        /** DBUS interface function.
             * Focus inward
             * @param ms If set, focus inward for ms ticks (Absolute Focuser), or ms milliseconds (Relative Focuser). If not set, it will use the value specified in the options.
             */
        Q_SCRIPTABLE bool focusIn(int ms = -1);

        /** DBUS interface function.
             * Focus outward
             * @param ms If set, focus outward for ms ticks (Absolute Focuser), or ms milliseconds (Relative Focuser). If not set, it will use the value specified in the options.
             */
        Q_SCRIPTABLE bool focusOut(int ms = -1);

        /**
             * @brief checkFocus Given the minimum required HFR, check focus and calculate HFR. If current HFR exceeds required HFR, start autofocus process, otherwise do nothing.
             * @param requiredHFR Minimum HFR to trigger autofocus process.
             */
        Q_SCRIPTABLE Q_NOREPLY void checkFocus(double requiredHFR);

        /** @}*/

        /**
             * @brief startFraming Begins continuous capture of the CCD and calculates HFR every frame.
             */
        void startFraming();

        /**
         * @brief Move the focuser to the initial focus position.
         */
        void resetFocuser();

        /**
             * @brief checkStopFocus Perform checks before stopping the autofocus operation. Some checks are necessary for in-sequence focusing.
             * @param abort true iff focusing should be aborted, false if it should only be stopped and marked as failed
             */
        void checkStopFocus(bool abort);

        /**
         * @brief React when a meridian flip has been started
         */
        void meridianFlipStarted();

        /**
             * @brief Check CCD and make sure information is updated accordingly. This simply calls syncCameraInfo for the current CCD.
             * @param CCDNum By default, we check the already selected CCD in the dropdown menu. If CCDNum is specified, the check is made against this specific CCD in the dropdown menu.
             *  CCDNum is the index of the CCD in the dropdown menu.
             */
        void checkCamera();

        /**
             * @brief syncCameraInfo Read current CCD information and update settings accordingly.
             */
        void syncCameraInfo();

        /**
         * @brief Update camera controls like Gain, ISO, Offset...etc
         */
        void syncCCDControls();

        /**
             * @brief Check Focuser and make sure information is updated accordingly.
             * @param FocuserNum By default, we check the already selected focuser in the dropdown menu. If FocuserNum is specified, the check is made against this specific focuser in the dropdown menu.
             *  FocuserNum is the index of the focuser in the dropdown menu.
             */
        void checkFocuser();

        /**
             * @brief Check Filter and make sure information is updated accordingly.
             * @param filterNum By default, we check the already selected filter in the dropdown menu. If filterNum is specified, the check is made against this specific filter in the dropdown menu.
             *  filterNum is the index of the filter in the dropdown menu.
             */
        void checkFilter();

        /**
             * @brief Check temperature source and make sure information is updated accordingly.
             * @param name Name of temperature source, if empty then use current source.
             */
        void checkTemperatureSource(const QString &name = QString());

        /**
             * @brief clearDataPoints Remove all data points from HFR plots
             */
        void clearDataPoints();

        /**
             * @brief focusStarSelected The user selected a focus star, save its coordinates and subframe it if subframing is enabled.
             * @param x X coordinate
             * @param y Y coordinate
             */
        void focusStarSelected(int x, int y);

        /**
         * @brief selectFocusStarFraction Select the focus star based by fraction of the overall size.
         * It calls focusStarSelected after multiplying the fractions (0.0 to 1.0) with the focus view width and height.
         * @param x final x = x * focusview_width
         * @param y final y = y * focusview_height
         */
        void selectFocusStarFraction(double x, double y);

        /**
             * @brief newFITS A new FITS blob is received by the CCD driver.
             * @param bp pointer to blob data
             */
        void processData(const QSharedPointer<FITSData> &data);

        /**
             * @brief processFocusNumber Read focus number properties of interest as they arrive from the focuser driver and process them accordingly.
             * @param nvp pointer to updated focuser number property.
             */
        void processFocusNumber(INumberVectorProperty *nvp);

        /**
             * @brief processTemperatureSource Updates focus temperature source.
             * @param nvp pointer to updated focuser number property.
             */
        void processTemperatureSource(INumberVectorProperty *nvp);

        /**
             * @brief setFocusStatus Upon completion of the focusing process, set its status (fail or pass) and reset focus process to clean state.
             * @param status If true, the focus process finished successfully. Otherwise, it failed.
             */
        //void setAutoFocusResult(bool status);

        // Logging methods - one for INFO messages to the kstars log, and one for a CSV auto-focus log
        void appendLogText(const QString &);
        void appendFocusLogText(const QString &);

        // Adjust focuser offset, relative or absolute
        void adjustFocusOffset(int value, bool useAbsoluteOffset);

        // Update Mount module status
        void setMountStatus(ISD::Mount::Status newState);

        // Update Altitude From Mount
        void setMountCoords(const SkyPoint &position, ISD::Mount::PierSide pierSide, const dms &ha);

        /**
         * @brief toggleVideo Turn on and off video streaming if supported by the camera.
         * @param enabled Set to true to start video streaming, false to stop it if active.
         */
        void toggleVideo(bool enabled);

        /**
         * @brief setWeatherData Updates weather data that could be used to extract focus temperature from observatory
         * in case focus native temperature is not available.
         */
        //void setWeatherData(const std::vector<ISD::Weather::WeatherData> &data);

        /**
         * @brief loadOptionsProfiles Load StellarSolver Profile
         */
        void loadStellarSolverProfiles();

        /**
         * @brief getStellarSolverProfiles
         * @return list of StellarSolver profile names
         */
        QStringList getStellarSolverProfiles();



    protected:
        void addPlotPosition(int pos, double hfr, bool plot = true);

    private slots:
        /**
             * @brief toggleSubframe Process enabling and disabling subfrag.
             * @param enable If true, subframing is enabled. If false, subframing is disabled. Even if subframing is enabled, it must be supported by the CCD driver.
             */
        void toggleSubframe(bool enable);

        void checkAutoStarTimeout();

        void setAbsoluteFocusTicks();

        void updateBoxSize(int value);

        void processCaptureTimeout();

        void processCaptureError(ISD::Camera::ErrorType type);

        void setCaptureComplete();

        void showFITSViewer();

        void toggleFocusingWidgetFullScreen();

        void setVideoStreamEnabled(bool enabled);

        void syncSettings();

        void calculateHFR();
        void setCurrentHFR(double value);

    signals:
        void newLog(const QString &text);
        void newStatus(Ekos::FocusState state);
        void newHFR(double hfr, int position);
        void newFocusTemperatureDelta(double delta, double absTemperature);

        void absolutePositionChanged(int value);
        void focusPositionAdjusted();

        void suspendGuiding();
        void resumeGuiding();
        void newImage(const QSharedPointer<FITSView> &view);
        void newStarPixmap(QPixmap &);
        void settingsUpdated(const QVariantMap &settings);

        // Signals for Analyze.
        void autofocusStarting(double temperature, const QString &filter);
        void autofocusComplete(const QString &filter, const QString &points);
        void autofocusAborted(const QString &filter, const QString &points);

        // HFR V curve plot events
        /**
         * @brief initialize the HFR V plot
         * @param showPosition show focuser position (true) or count focus iterations (false)
         */
        void initHFRPlot(bool showPosition);

        /**
          * @brief new HFR plot position
          * @param pos focuser position
          * @param hfr measured star HFR value
          * @param pulseDuration Pulse duration in ms for relative focusers that only support timers,
          *        or the number of ticks in a relative or absolute focuser
          * */
        void newHFRPlotPosition(double pos, double hfr, int pulseDuration, bool plot = true);

        /**
          * @brief new HFR plot position with sigma
          * @param pos focuser position with associated error (sigma)
          * @param hfr measured star HFR value
          * @param sigma is the standard deviation of star HFRs
          * @param pulseDuration Pulse duration in ms for relative focusers that only support timers,
          *        or the number of ticks in a relative or absolute focuser
          * */
        void newHFRPlotPositionWithSigma(double pos, double hfr, double sigma, int pulseDuration, bool plot = true);

        /**
         * @brief draw the approximating polynomial into the HFR V-graph
         * @param poly pointer to the polynomial approximation
         * @param isVShape has the solution a V shape?
         * @param activate make the graph visible?
         */
        void drawPolynomial(PolynomialFit *poly, bool isVShape, bool activate, bool plot = true);

        /**
         * @brief draw the curve into the HFR V-graph
         * @param poly pointer to the polynomial approximation
         * @param isVShape has the solution a V shape?
         * @param activate make the graph visible?
         */
        void drawCurve(CurveFitting *curve, bool isVShape, bool activate, bool plot = true);

        /**
         * @brief Focus solution with minimal HFR found
         * @param solutionPosition focuser position
         * @param solutionValue HFR value
         */
        void minimumFound(double solutionPosition, double solutionValue, bool plot = true);

        /**
         * @brief redraw the entire HFR plot
         * @param poly pointer to the polynomial approximation
         * @param solutionPosition solution focuser position
         * @param solutionValue solution HFR value
         */
        void redrawHFRPlot(PolynomialFit *poly, double solutionPosition, double solutionValue);

        /**
         * @brief draw a title on the focus plot
         * @param title the title
         */
        void setTitle(const QString &title, bool plot = true);

        /**
         * @brief update the title on the focus plot
         * @param title
         */
        void updateTitle(const QString &title, bool plot = true);

    private:

        QList<SSolver::Parameters> m_StellarSolverProfiles;
        QString savedOptionsProfiles;
        StellarSolverProfileEditor *optionsProfileEditor { nullptr };

        ////////////////////////////////////////////////////////////////////
        /// Connections
        ////////////////////////////////////////////////////////////////////
        void initConnections();

        ////////////////////////////////////////////////////////////////////
        /// Settings
        ////////////////////////////////////////////////////////////////////

        /**
         * @brief Connect GUI elements to sync settings once updated.
         */
        void connectSettings();
        /**
         * @brief Stop updating settings when GUI elements are updated.
         */
        void disconnectSettings();
        /**
         * @brief loadSettings Load setting from Options and set them accordingly.
         */
        void loadGlobalSettings();

        ////////////////////////////////////////////////////////////////////
        /// HFR Plot
        ////////////////////////////////////////////////////////////////////
        void initPlots();

        ////////////////////////////////////////////////////////////////////
        /// Positions
        ////////////////////////////////////////////////////////////////////
        void getAbsFocusPosition();
        bool autoFocusChecks();
        void autoFocusAbs();
        void autoFocusLinear();
        void autoFocusRel();

        // Linear does plotting differently from the rest.
        void plotLinearFocus();

        // Linear final updates to the curve
        void plotLinearFinalUpdates();

        /** @brief Helper function determining whether the focuser behaves like a position
         *         based one (vs. a timer based)
         */
        bool isPositionBased()
        {
            return (canAbsMove || canRelMove || (m_FocusAlgorithm == FOCUS_LINEAR) || (m_FocusAlgorithm == FOCUS_LINEAR1PASS));
        }
        void resetButtons();
        void stop(FocusState completionState = FOCUS_ABORTED);

        void initView();

        /**
         * @brief prepareCapture Set common settings for capture for focus module
         * @param targetChip target Chip
         */
        void prepareCapture(ISD::CameraChip *targetChip);
        ////////////////////////////////////////////////////////////////////
        /// HFR
        ////////////////////////////////////////////////////////////////////
        void setHFRComplete();

        // Sets the algorithm and enables/disables various UI inputs.
        void setFocusAlgorithm(Algorithm algorithm);

        void setCurveFit(CurveFitting::CurveFit curvefit);

        // Move the focuser in (negative) or out (positive amount).
        bool changeFocus(int amount);

        // Start up capture, or occasionally move focuser again, after current focus-move accomplished.
        void autoFocusProcessPositionChange(IPState state);

        // For the Linear algorithm, which always scans in (from higher position to lower position)
        // if we notice the new position is higher than the current position (that is, it is the start
        // of a new scan), we adjust the new position to be several steps further out than requested
        // and set focuserAdditionalMovement to the extra motion, so that after this motion completes
        // we will then scan back in (back to the originally requested position). This "dance" is done
        // to reduce backlash on such movement changes and so that we've always focused in before capture.
        // For LINEAR1PASS algo use the user-defined backlash value to adjust by
        int adjustLinearPosition(int position, int newPosition, int backlash);

        /**
         * @brief syncTrackingBoxPosition Sync the tracking box to the current selected star center
         */
        void syncTrackingBoxPosition();

        /** @internal Search for stars using the method currently configured, and return the consolidated HFR.
         * @param image_data is the FITS frame to work with.
         * @return the HFR of the star or field of stars in the frame, depending on the consolidation method, or -1 if it cannot be estimated.
         */
        void analyzeSources();

        /** @internal Add a new HFR for the current focuser position.
         * @param newHFR is the new HFR to consider for the current focuser position.
         * @return true if a new sample is required, else false.
         */
        bool appendHFR(double newHFR);


        /**
         * @brief completeAutofocusProcedure finishes off autofocus and emits a message for other modules.
         */
        void completeFocusProcedure(FocusState completionState, bool plot = true);

        /**
         * @brief activities to be executed after the configured settling time
         * @param completionState state the focuser completed with
         * @param autoFocusUsed is autofocus running?
         */
        void settle(const FocusState completionState, const bool autoFocusUsed);

        void setLastFocusTemperature();
        bool findTemperatureElement(const QSharedPointer<ISD::GenericDevice> &device);

        bool syncControl(const QVariantMap &settings, const QString &key, QWidget * widget);

        void setupOpticalTrainManager();
        void refreshOpticalTrain();
        QString opticalTrain() const
        {
            return opticalTrainCombo->currentText();
        }
        void setOpticalTrain(const QString &value)
        {
            opticalTrainCombo->setCurrentText(value);
        }

        /**
         * @brief handleFocusMotionTimeout When focuser is command to go to a target position, we expect to receive a notification
         * that it arrived at the desired destination. If not, we command it again.
         */
        void handleFocusMotionTimeout();

        /// Focuser device needed for focus operation
        ISD::Focuser *m_Focuser { nullptr };
        /// CCD device needed for focus operation
        ISD::Camera *m_Camera { nullptr };
        /// Optional device filter
        ISD::FilterWheel *m_FilterWheel { nullptr };
        /// Optional temperature source element
        INumber *currentTemperatureSourceElement {nullptr};

        /// Current filter position
        int currentFilterPosition { -1 };
        int fallbackFilterPosition { -1 };
        /// True if we need to change filter position and wait for result before continuing capture
        bool filterPositionPending { false };
        bool fallbackFilterPending { false };

        /// They're generic GDInterface because they could be either ISD::Camera or ISD::FilterWheel or ISD::Weather
        QList<QSharedPointer<ISD::GenericDevice>> m_TemperatureSources;

        /// As the name implies
        Direction m_LastFocusDirection { FOCUS_NONE };
        /// Keep track of the last requested steps
        uint32_t m_LastFocusSteps {0};
        /// What type of focusing are we doing right now?
        Type m_FocusType { FOCUS_MANUAL };
        /// Focus HFR & Centeroid algorithms
        StarAlgorithm m_FocusDetection { ALGORITHM_SEP };
        /// Focus Process Algorithm
        Algorithm m_FocusAlgorithm { FOCUS_LINEAR1PASS };
        /// Curve fit, default to Quadratic
        CurveFitting::CurveFit m_CurveFit { CurveFitting::FOCUS_QUADRATIC };

        /*********************
         * HFR Club variables
         *********************/

        /// Current HFR value just fetched from FITS file
        double currentHFR { 0 };
        /// Last HFR value recorded
        double lastHFR { 0 };
        /// If (currentHFR > deltaHFR) we start the autofocus process.
        double minimumRequiredHFR { -1 };
        /// Maximum HFR recorded
        double maxHFR { 1 };
        /// Is HFR increasing? We're going away from the sweet spot! If HFRInc=1, we re-capture just to make sure HFR calculations are correct, if HFRInc > 1, we switch directions
        int HFRInc { 0 };
        /// If HFR decreasing? Well, good job. Once HFR start decreasing, we can start calculating HFR slope and estimating our next move.
        int HFRDec { 0 };

        /****************************
         * Absolute position focusers
         ****************************/
        /// Absolute focus position
        int currentPosition { 0 };
        /// Motion state of the absolute focuser
        IPState currentPositionState {IPS_IDLE};
        /// What was our position before we started the focus process?
        int initialFocuserAbsPosition { -1 };
        /// Pulse duration in ms for relative focusers that only support timers, or the number of ticks in a relative or absolute focuser
        int pulseDuration { 1000 };
        /// Does the focuser support absolute motion?
        bool canAbsMove { false };
        /// Does the focuser support relative motion?
        bool canRelMove { false };
        /// Does the focuser support timer-based motion?
        bool canTimerMove { false };
        /// Maximum range of motion for our lovely absolute focuser
        double absMotionMax { 0 };
        /// Minimum range of motion for our lovely absolute focuser
        double absMotionMin { 0 };
        /// How many iterations have we completed now in our absolute autofocus algorithm? We can't go forever
        int absIterations { 0 };

        /****************************
         * Misc. variables
         ****************************/

        /// Are we in the process of capturing an image?
        bool captureInProgress { false };
        /// Are we in the process of calculating HFR?
        bool hfrInProgress { false };
        // Was the frame modified by us? Better keep track since we need to return it to its previous state once we are done with the focus operation.
        //bool frameModified;
        /// Was the modified frame subFramed?
        bool subFramed { false };
        /// If the autofocus process fails, let's not ruin the capture session probably taking place in the next tab. Instead, we should restart it and try again, but we keep count until we hit MAXIMUM_RESET_ITERATIONS
        /// and then we truly give up.
        int resetFocusIteration { 0 };
        /// Which filter must we use once the autofocus process kicks in?
        int lockedFilterIndex { -1 };
        /// Keep track of what we're doing right now
        bool inAutoFocus { false };
        bool inFocusLoop { false };
        //bool inSequenceFocus { false };
        /// Keep track of request to retry or abort an AutoFocus run after focus position has been reset
        /// RESTART_NONE = normal operation, no restart
        /// RESTART_NOW = restart the autofocus routine
        /// RESTART_ABORT = when autofocus has been tried MAXIMUM_RESET_ITERATIONS times, abort the routine
        typedef enum { RESTART_NONE = 0, RESTART_NOW, RESTART_ABORT } FocusRestartState;
        FocusRestartState m_RestartState { RESTART_NONE };
        /// Did we reverse direction?
        bool reverseDir { false };
        /// Did the user or the auto selection process finish selecting our focus star?
        bool starSelected { false };
        /// Adjust the focus position to a target value
        bool adjustFocus { false };
        // Target frame dimensions
        //int fx,fy,fw,fh;
        /// If HFR=-1 which means no stars detected, we need to decide how many times should the re-capture process take place before we give up or reverse direction.
        int noStarCount { 0 };
        /// Track which upload mode the CCD is set to. If set to UPLOAD_LOCAL, then we need to switch it to UPLOAD_CLIENT in order to do focusing, and then switch it back to UPLOAD_LOCAL
        ISD::Camera::UploadMode rememberUploadMode { ISD::Camera::UPLOAD_CLIENT };
        /// Previous binning setting
        int m_ActiveBin { 1 };
        /// HFR values for captured frames before averages
        QVector<double> HFRFrames;
        // Camera Fast Exposure
        bool m_RememberCameraFastExposure = { false };
        // Future Watch
        QFutureWatcher<bool> m_StarFinderWatcher;
        // R2 as a measure of how well the curve fits the datapoints. Passed to the V-curve graph for display
        double R2 = 0;
        // Counter to retry the auto focus run if the R2Limit has not been reached
        int R2Retries = 0;

        /// Autofocus log file info.
        QStringList m_LogText;
        QFile m_FocusLogFile;
        QString m_FocusLogFileName;
        bool m_FocusLogEnabled { false };

        ITextVectorProperty *filterName { nullptr };
        INumberVectorProperty *filterSlot { nullptr };

        /****************************
         * Plot variables
         ****************************/

        /// Plot minimum positions
        double minPos { 1e6 };
        /// Plot maximum positions
        double maxPos { 0 };

        /// HFR V curve plot points
        QVector<double> hfr_position, hfr_value;
        bool isVShapeSolution = false;

        /// State
        Ekos::FocusState state { Ekos::FOCUS_IDLE };

        /// CCD Chip frame settings
        QMap<ISD::CameraChip *, QVariantMap> frameSettings;

        /// Selected star coordinates
        QVector3D starCenter;

        // Remember last star center coordinates in case of timeout in manual select mode
        QVector3D rememberStarCenter;

        /// Focus Frame
        QSharedPointer<FITSView> m_FocusView;

        /// Star Select Timer
        QTimer waitStarSelectTimer;

        /// FITS Viewer in case user want to display in it instead of internal view
        QPointer<FITSViewer> fv;

        /// Track star position and HFR to know if we're detecting bogus stars due to detection algorithm false positive results
        QVector<QVector3D> starsHFR;

        /// Relative Profile
        FocusProfilePlot *profilePlot { nullptr };
        QDialog *profileDialog { nullptr };

        /// Polynomial fitting.
        std::unique_ptr<PolynomialFit> polynomialFit;

        // Curve fitting.
        std::unique_ptr<CurveFitting> curveFitting;

        // Capture timers
        QTimer captureTimer;
        QTimer captureTimeout;
        uint8_t captureTimeoutCounter { 0 };
        uint8_t captureFailureCounter { 0 };

        // Focus motion timer.
        QTimer m_FocusMotionTimer;
        uint8_t m_FocusMotionTimerCounter {0};

        // Guide Suspend
        bool m_GuidingSuspended { false };

        // Data
        QSharedPointer<FITSData> m_ImageData;

        // Linear focuser.
        std::unique_ptr<FocusAlgorithmInterface> linearFocuser;
        int focuserAdditionalMovement { 0 };
        int linearRequestedPosition { 0 };

        bool hasDeviation { false };

        //double observatoryTemperature { INVALID_VALUE };
        double m_LastSourceAutofocusTemperature { INVALID_VALUE };
        //TemperatureSource lastFocusTemperatureSource { NO_TEMPERATURE };

        // Mount altitude value for logging
        double mountAlt { INVALID_VALUE };

        static constexpr uint8_t MAXIMUM_FLUCTUATIONS {10};

        QVariantMap m_Settings;
        QVariantMap m_GlobalSettings;

        // Dark Processor
        QPointer<DarkProcessor> m_DarkProcessor;

        QSharedPointer<FilterManager> m_FilterManager;
};
}
