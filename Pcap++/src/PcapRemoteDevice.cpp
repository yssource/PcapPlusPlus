#if defined(WIN32) || defined(WINx64) || defined(PCAPPP_MINGW_ENV)

#define LOG_MODULE PcapLogModuleRemoteDevice

#include "PcapRemoteDevice.h"
#include "Logger.h"
#include "pcap.h"


namespace pcpp
{

pcap_rmtauth PcapRemoteAuthentication::getPcapRmAuth() const
{
	pcap_rmtauth result;
	result.type = RPCAP_RMTAUTH_PWD;
	result.username = (char*)userName.c_str();
	result.password = (char*)password.c_str();
	return result;
}

PcapRemoteDevice::PcapRemoteDevice(pcap_if_t* iface, PcapRemoteAuthentication* remoteAuthentication, const IPAddress& remoteMachineIP, uint16_t remoteMachinePort)
	: PcapLiveDevice(iface, false, false, false)
{
	LOG_DEBUG("MTU calculation isn't supported for remote devices. Setting MTU to 1514");
	m_DeviceMtu = 1514;
	m_RemoteMachineIpAddress = remoteMachineIP;
	m_RemoteMachinePort = remoteMachinePort;
	m_RemoteAuthentication = remoteAuthentication;
}


bool PcapRemoteDevice::open()
{
	char errbuf[PCAP_ERRBUF_SIZE];
	int flags = PCAP_OPENFLAG_PROMISCUOUS | PCAP_OPENFLAG_NOCAPTURE_RPCAP; //PCAP_OPENFLAG_DATATX_UDP doesn't always work
	LOG_DEBUG("Opening device '" << m_Name << "'");
	pcap_rmtauth* pRmAuth = NULL;
	pcap_rmtauth rmAuth;
	if (m_RemoteAuthentication != NULL)
	{
		rmAuth = m_RemoteAuthentication->getPcapRmAuth();
		pRmAuth = &rmAuth;
	}

	m_PcapDescriptor = pcap_open(m_Name.c_str(), PCPP_MAX_PACKET_SIZE, flags, 250, pRmAuth, errbuf);
	if (m_PcapDescriptor == NULL)
	{
		LOG_ERROR("Error opening device. Error was: " << errbuf);
		m_DeviceOpened = false;
		return false;
	}

	//in Remote devices there shouldn't be 2 separate descriptors
	m_PcapSendDescriptor = m_PcapDescriptor;

	//setFilter requires that m_DeviceOpened == true
	m_DeviceOpened = true;

	//for some reason if a filter is not set than WinPcap throws an exception. So Here is a generic filter that catches all traffic
	if (!setFilter("ether proto (\\ip or \\ip6 or \\arp or \\rarp or \\decnet or \\sca or \\lat or \\mopdl or \\moprc or \\iso or \\stp or \\ipx or \\netbeui or 0x80F3)")) //0x80F3 == AARP
	{
		LOG_ERROR("Error setting the filter. Error was: " << Logger::getInstance().getLastError());
		m_DeviceOpened = false;
		return false;
	}

	LOG_DEBUG("Device '" << m_Name << "' opened");

	return true;
}

void* PcapRemoteDevice::remoteDeviceCaptureThreadMain(void *ptr)
{
	PcapRemoteDevice* pThis = (PcapRemoteDevice*)ptr;
	if (pThis == NULL)
	{
		LOG_ERROR("Capture thread: Unable to extract PcapLiveDevice instance");
		return 0;
	}

	LOG_DEBUG("Started capture thread for device '" << pThis->m_Name << "'");

	pcap_pkthdr* pkthdr;
	const uint8_t* pktData;

	if (pThis->m_CaptureCallbackMode)
	{
		while (!pThis->m_StopThread)
		{
			if (pcap_next_ex(pThis->m_PcapDescriptor, &pkthdr, &pktData) > 0)
				onPacketArrives((uint8_t*)pThis, pkthdr, pktData);
		}
	}
	else
	{
		while (!pThis->m_StopThread)
		{
			if (pcap_next_ex(pThis->m_PcapDescriptor, &pkthdr, &pktData) > 0)
				onPacketArrivesNoCallback((uint8_t*)pThis, pkthdr, pktData);
		}
	}
	LOG_DEBUG("Ended capture thread for device '" << pThis->m_Name << "'");
	return 0;
}

ThreadStart PcapRemoteDevice::getCaptureThreadStart()
{
	return &remoteDeviceCaptureThreadMain;
}

void PcapRemoteDevice::getStatistics(PcapStats& stats) const
{
	int allocatedMemory;
	pcap_stat* tempStats = pcap_stats_ex(m_PcapDescriptor, &allocatedMemory);
	if (allocatedMemory < (int)sizeof(pcap_stat))
	{
		LOG_ERROR("Error getting statistics from live device '" << m_Name << "': WinPcap did not allocate the entire struct");
		return;
	}
	stats.packetsRecv = tempStats->ps_capt;
	stats.packetsDrop = tempStats->ps_drop + tempStats->ps_netdrop;
	stats.packetsDropByInterface = tempStats->ps_ifdrop;
}

uint32_t PcapRemoteDevice::getMtu() const
{
	LOG_DEBUG("MTU isn't supported for remote devices");
	return 0;
}

MacAddress PcapRemoteDevice::getMacAddress() const
{
	LOG_ERROR("MAC address isn't supported for remote devices");
	return MacAddress::Zero;
}

} // namespace pcpp

#endif // WIN32 || WINx64
