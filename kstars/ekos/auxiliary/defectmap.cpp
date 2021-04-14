/*  Defect Map
    Copyright (C) 2021 Jasem Mutlaq <mutlaqja@ikarustech.com>

    This application is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.
 */

#include "defectmap.h"
#include <QJsonDocument>

//////////////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////////////
QJsonObject BadPixel::json() const
{
    QJsonObject object
    {
        {"x", x},
        {"y", y},
        {"value", value}
    };
    return object;
}

//////////////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////////////
DefectMap::DefectMap() : QObject()
{

}

//////////////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////////////
bool DefectMap::load(const QString &filename)
{
    QFile input(filename);
    if (!input.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError parserError;
    auto doc = QJsonDocument::fromJson(input.readAll(), &parserError);
    if (parserError.error != QJsonParseError::NoError)
        return false;

    m_Filename = filename;

    auto root = doc.object();

    m_Camera = root["camera"].toString();
    m_Median = root["median"].toDouble();
    m_StandardDeviation = root["standardDeviation"].toDouble();
    m_HotPixelsAggressiveness = root["hotAggressiveness"].toInt();
    m_ColdPixelsAggressiveness = root["coldAggressiveness"].toInt();

    QJsonArray hot = root["hot"].toArray();
    QJsonArray cold = root["cold"].toArray();

    m_HotPixels.clear();
    m_ColdPixels.clear();

    for (const auto &onePixel : qAsConst(hot))
    {
        QJsonObject oneObject = onePixel.toObject();
        m_HotPixels.insert(BadPixel(oneObject["x"].toInt(), oneObject["y"].toInt(), oneObject["value"].toDouble()));
    }

    m_HotPixelsCount = m_HotPixels.size();

    for (const auto &onePixel : qAsConst(cold))
    {
        QJsonObject oneObject = onePixel.toObject();
        m_ColdPixels.insert(BadPixel(oneObject["x"].toInt(), oneObject["y"].toInt(), oneObject["value"].toDouble()));
    }

    m_ColdPixelsCount = m_ColdPixels.size();
    return true;
}

//////////////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////////////
bool DefectMap::save(const QString &filename, const QString &camera)
{
    QJsonObject root;

    root.insert("camera", camera);
    root.insert("median", m_Median);
    root.insert("standardDeviation", m_StandardDeviation);
    root.insert("hotAggressiveness", m_HotPixelsAggressiveness);
    root.insert("coldAggressiveness", m_ColdPixelsAggressiveness);

    QJsonArray hotArray, coldArray;
    for (const auto &onePixel : m_HotPixels)
        hotArray.append(onePixel.json());
    for (const auto &onePixel : m_ColdPixels)
        hotArray.append(onePixel.json());

    root.insert("hot", hotArray);
    root.insert("cold", coldArray);

    QFile output(filename);

    if (!output.open(QIODevice::WriteOnly))
        return false;

    output.write(QJsonDocument(root).toJson());
    output.close();

    return true;
}

//////////////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////////////
double DefectMap::calculateSigma(uint8_t aggressiveness)
{
    // Estimated power law fitting to compress values within 0.25 to 5 sigma range
    return 4.9862127851 * exp(-0.0003088013 * (aggressiveness - 1.6840377227) * (aggressiveness - 1.6840377227));
}

//////////////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////////////
void DefectMap::setDarkData(const QSharedPointer<FITSData> &data)
{
    m_DarkData = data;
    m_Median = data->getMedian(0);
    m_StandardDeviation = data->getStdDev(0);
    if (m_HotPixels.empty() && m_ColdPixels.empty())
        initBadPixels();
    else
        filterPixels();
}

//////////////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////////////
double DefectMap::getHotThreshold(uint8_t aggressiveness)
{
    return (m_Median + m_StandardDeviation * calculateSigma(aggressiveness));
}

//////////////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////////////
double DefectMap::getColdThreshold(uint8_t aggressiveness)
{
    return (m_Median - m_StandardDeviation * calculateSigma(aggressiveness));
}

//////////////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////////////
void DefectMap::initBadPixels()
{
    double hotPixelThreshold =  getHotThreshold(100);
    double coldPixelThreshold = getColdThreshold(100);

    m_ColdPixels.clear();
    m_HotPixels.clear();

    switch (m_DarkData->dataType())
    {
        case TBYTE:
            initBadPixelsInternal<uint8_t>(hotPixelThreshold, coldPixelThreshold);
            break;

        case TSHORT:
            initBadPixelsInternal<int16_t>(hotPixelThreshold, coldPixelThreshold);
            break;

        case TUSHORT:
            initBadPixelsInternal<uint16_t>(hotPixelThreshold, coldPixelThreshold);
            break;

        case TLONG:
            initBadPixelsInternal<int32_t>(hotPixelThreshold, coldPixelThreshold);
            break;

        case TULONG:
            initBadPixelsInternal<uint32_t>(hotPixelThreshold, coldPixelThreshold);
            break;

        case TFLOAT:
            initBadPixelsInternal<float>(hotPixelThreshold, coldPixelThreshold);
            break;

        case TLONGLONG:
            initBadPixelsInternal<int64_t>(hotPixelThreshold, coldPixelThreshold);
            break;

        case TDOUBLE:
            initBadPixelsInternal<double>(hotPixelThreshold, coldPixelThreshold);
            break;

        default:
            break;
    }
}

//////////////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////////////
template <typename T>
void DefectMap::initBadPixelsInternal(double hotPixelThreshold,
                                      double coldPixelThreshold)
{
    T const *buffer  = reinterpret_cast<T const*>(m_DarkData->getImageBuffer());
    const uint32_t width = m_DarkData->width();
    const uint32_t height = m_DarkData->height();
    // Skip 10% of edge
    //    const uint32_t offsetX = width / 10;
    //    const uint32_t offsetY = height / 10;

    // Need templated function
    for (uint32_t y = 5; y < height - 5; y++)
    {
        for (uint32_t x = 5; x < width - 5; x++)
        {
            uint32_t offset = x + y * width;
            if (buffer[offset] > hotPixelThreshold)
                m_HotPixels.insert(BadPixel(x, y, buffer[offset]));
            else if (buffer[offset] < coldPixelThreshold)
                m_ColdPixels.insert(BadPixel(x, y, buffer[offset]));
        }
    }

    filterPixels();
}

//////////////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////////////
void DefectMap::filterPixels()
{
    double hotPixelThreshold =  getHotThreshold(m_HotPixelsAggressiveness);
    double coldPixelThreshold = getColdThreshold(m_ColdPixelsAggressiveness);

    m_HotPixelsThreshold = m_HotPixels.lower_bound(BadPixel(0, 0, hotPixelThreshold));
    m_ColdPixelsThreshold = m_ColdPixels.lower_bound(BadPixel(0, 0, coldPixelThreshold));

    if (m_HotPixelsThreshold == m_HotPixels.cend())
        m_HotPixelsCount = 0;
    else
        m_HotPixelsCount = std::distance(m_HotPixelsThreshold, m_HotPixels.cend());

    if (m_ColdPixelsThreshold == m_ColdPixels.cbegin())
        m_ColdPixelsCount = 0;
    else
        m_ColdPixelsCount = std::distance(m_ColdPixels.cbegin(), m_ColdPixelsThreshold);

    emit pixelsUpdated(m_HotPixelsCount, m_ColdPixelsCount);
}

//////////////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////////////
void DefectMap::setHotEnabled(bool enabled)
{
    m_HotEnabled = enabled;
    emit pixelsUpdated(m_HotEnabled ? m_HotPixelsCount : 0, m_ColdPixelsCount);
}

//////////////////////////////////////////////////////////////////////////////
///
//////////////////////////////////////////////////////////////////////////////
void DefectMap::setColdEnabled(bool enabled)
{
    m_ColdEnabled = enabled;
    emit pixelsUpdated(m_HotPixelsCount, m_ColdEnabled ? m_ColdPixelsCount : 0);
}
