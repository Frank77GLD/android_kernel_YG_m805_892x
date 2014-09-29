/** @file
 */
#include "os_dep.h"
#include "hw.h"
#include "usb.h"
#include "pcd.h"
#include "driver.h"
#include "cil.h"

#ifdef DWC_UTE
# include "ute_if.h"
#endif

#ifdef DWC_MULTI_GADGET
# define FSTOR_NUM_GADGETS 4
#else
# define FSTOR_NUM_GADGETS 1
#endif

#include <mach/reg_physical.h>
#include <mach/structures_hsio.h>

#define dwc_wait_if_hibernating(_pcd, _flags)				\
{									\
	int _temp = atomic_read(&(_pcd)->usb3_dev->hibernate);		\
	while (_temp >= DWC_HIBER_SLEEPING &&				\
	       _temp != DWC_HIBER_SS_DIS_QUIRK) {			\
		spin_unlock_irqrestore(&(_pcd)->lock, (_flags));	\
		msleep(1);						\
		spin_lock_irqsave(&(_pcd)->lock, (_flags));		\
		_temp = atomic_read(&(_pcd)->usb3_dev->hibernate);	\
	}								\
}

/**
 * NOTE: These strings MUST reflect the number and type of endpoints that the
 * core was configured with in CoreConsultant, and their intended usage. In
 * particular, the type (bulk/int/iso) determines how many TRBs will be
 * allocated for each endpoint. See gadget_init_eps() and trb_alloc() below.
 *
 * Naming convention is described in drivers/usb/gadget/epautoconf.c
 */
static const char *g_ep_names[] = { "ep0", "ep1out", "ep1in", "ep2out",
				    "ep2in", "ep3out", "ep3in" };

/*=======================================================================*/
/*
 * Linux-specific EP0 functions
 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0) && !defined(DWC_BOS_IN_GADGET)

/** The BOS Descriptor */

static struct usb_dev_cap_20_ext_desc cap1 = {
	sizeof(struct usb_dev_cap_20_ext_desc),	/* bLength */
	UDESC_DEVICE_CAPABILITY,		/* bDescriptorType */
	USB_DEVICE_CAPABILITY_20_EXTENSION,	/* bDevCapabilityType */
	UCONSTDW(0x2),				/* bmAttributes */
};

static struct usb_dev_cap_ss_usb cap2 = {
	sizeof(struct usb_dev_cap_ss_usb),	/* bLength */
	UDESC_DEVICE_CAPABILITY,		/* bDescriptorType */
	USB_DEVICE_CAPABILITY_SS_USB,		/* bDevCapabilityType */
	0x0,					/* bmAttributes */
	UCONSTW(USB_DC_SS_USB_SPEED_SUPPORT_SS	/* wSpeedsSupported */
	    | USB_DC_SS_USB_SPEED_SUPPORT_HIGH),
	0x2,					/* bFunctionalitySupport */
	/* @todo set these to correct value */
	0xa,					/* bU1DevExitLat */
	UCONSTW(0x100),				/* wU2DevExitLat */
};

static struct usb_dev_cap_container_id cap3 = {
	sizeof(struct usb_dev_cap_container_id),/* bLength */
	UDESC_DEVICE_CAPABILITY,		/* bDescriptorType */
	USB_DEVICE_CAPABILITY_CONTAINER_ID,	/* bDevCapabilityType */
	0,					/* bReserved */
	/* @todo Create UUID */
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, /* containerID */
};

static struct wusb_bos_desc bos = {
	sizeof(struct wusb_bos_desc),		/* bLength */
	WUDESC_BOS,				/* bDescriptorType */
	UCONSTW(sizeof(struct wusb_bos_desc)	/* wTotalLength */
	    + sizeof(cap1) + sizeof(cap2) + sizeof(cap3)),
	3,					/* bNumDeviceCaps */
};
#endif

/**
 * This function processes the SET_ADDRESS Setup Commands.
 */
static void do_set_address(dwc_usb3_pcd_t *pcd)
{
	usb_device_request_t ctrl = pcd->ep0_setup_pkt->req;

	dwc_debug(pcd->usb3_dev, "SET ADDRESS\n");

	if (ctrl.bmRequestType == UT_DEVICE) {
#ifdef DEBUG_EP0
		dwc_debug(pcd->usb3_dev, "SET_ADDRESS %d\n",
			  UGETW(ctrl.wValue));
#endif
		dwc_usb3_set_address(pcd, UGETW(ctrl.wValue));
		pcd->ep0->dwc_ep.is_in = 1;
		pcd->ep0state = EP0_IN_WAIT_NRDY;
		if (ctrl.wValue)
			pcd->state = DWC_STATE_ADDRESSED;
		else
			pcd->state = DWC_STATE_DEFAULT;
	}
}

/**
 * This function stalls EP0.
 */
static void ep0_do_stall(dwc_usb3_pcd_t *pcd, int err_val)
{
	usb_device_request_t ctrl = pcd->ep0_setup_pkt->req;
	dwc_usb3_pcd_ep_t *ep0 = pcd->ep0;

	dwc_print(pcd->usb3_dev, "req %02x.%02x protocol STALL; err %d\n",
		  ctrl.bmRequestType, ctrl.bRequest, err_val);
	ep0->dwc_ep.is_in = 0;
	dwc_usb3_ep_set_stall(pcd, ep0);
	ep0->dwc_ep.stopped = 1;
	pcd->ep0state = EP0_IDLE;
	dwc_usb3_ep0_out_start(pcd);
}

/**
 * Clear the EP halt (STALL), and if there are pending requests start
 * the transfer.
 */
static void do_clear_halt(dwc_usb3_pcd_t *pcd, dwc_usb3_pcd_ep_t *ep)
{
	dwc_usb3_dev_ep_regs_t __iomem *ep_reg;

	dwc_debug(pcd->usb3_dev, "%s()\n", __func__);

	if (ep->dwc_ep.stall_clear_flag == 0) {
		dwc_usb3_ep_clear_stall(pcd, ep);
	} else {
		/* Clear sequence number using DEPCFG */
		if (ep->dwc_ep.is_in) {
			ep_reg = ep->dwc_ep.in_ep_reg;
			dwc_usb3_dep_cfg(pcd, ep_reg, ep->dwc_ep.param0in,
					 ep->dwc_ep.param1in, 0);
		} else {
			ep_reg = ep->dwc_ep.out_ep_reg;
			dwc_usb3_dep_cfg(pcd, ep_reg, ep->dwc_ep.param0out,
					 ep->dwc_ep.param1out, 0);
		}
	}

	if (ep->dwc_ep.stopped) {
		ep->dwc_ep.stopped = 0;

		/* If there is a request in the EP queue start it */
		if (ep != pcd->ep0 && ep->dwc_ep.is_in)
			dwc_usb3_os_start_next_request(pcd, ep);
	}

	/* Start Control Status Phase */
	pcd->ep0->dwc_ep.is_in = 1;
	pcd->ep0state = EP0_IN_WAIT_NRDY;
}

/**
 * This function delegates the setup command to the gadget driver.
 */
static int do_gadget_setup(dwc_usb3_pcd_t *pcd, usb_device_request_t *ctrl)
{
	int ret = -DWC_E_INVALID;

	dwc_debug(pcd->usb3_dev, "%s()\n", __func__);

	ret = dwc_usb3_pcd_setup(pcd, (void *)ctrl);
	if (ret < 0 && ret != -DWC_E_NOT_SUPPORTED)
		ep0_do_stall(pcd, ret);

	/* @todo This is a g_file_storage gadget driver specific
	 * workaround: a DELAYED_STATUS result from the fsg_setup
	 * routine will result in the gadget queueing a EP0 IN status
	 * phase for a two-stage control transfer. Exactly the same as
	 * a SET_CONFIGURATION/SET_INTERFACE except that this is a class
	 * specific request. Need a generic way to know when the gadget
	 * driver will queue the status phase. Can we assume when we
	 * call the gadget driver setup() function that it will always
	 * queue and require the following flag? Need to look into
	 * this.
	 */
	if (ret >= 256 + 999)
		pcd->request_config = 1;

	return ret;
}

/**
 * This function handles the Get Descriptor request for the BOS descriptor,
 * and passes all other requests to the Gadget Driver.
 */
static void do_get_descriptor(dwc_usb3_pcd_t *pcd)
{
	usb_device_request_t ctrl = pcd->ep0_setup_pkt->req;
	uint8_t dt = UGETW(ctrl.wValue) >> 8;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0) && !defined(DWC_BOS_IN_GADGET)
	uint16_t len = UGETW(ctrl.wLength);
	uint8_t *buf = pcd->ep0_status_buf;
#endif

#ifdef DEBUG_EP0
	dwc_debug(pcd->usb3_dev, "GET_DESCRIPTOR %02x.%02x v%04x i%04x l%04x\n",
		  ctrl.bmRequestType, ctrl.bRequest, UGETW(ctrl.wValue),
		  UGETW(ctrl.wIndex), UGETW(ctrl.wLength));
#endif

	switch (dt) {
	case UDESC_BOS:
		dwc_debug(pcd->usb3_dev, "\n\n\n\nGET_DESCRIPTOR(BOS)\n\n\n");
		if (pcd->speed != USB_SPEED_SUPER &&
		    pcd->usb3_dev->core_params->nobos) {
			ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
			return;
		}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0) && !defined(DWC_BOS_IN_GADGET)
		memcpy(buf, &bos, sizeof(bos));
		buf += sizeof(bos);
		memcpy(buf, &cap1, sizeof(cap1));
		buf += sizeof(cap1);
		memcpy(buf, &cap2, sizeof(cap2));
		buf += sizeof(cap2);
		memcpy(buf, &cap3, sizeof(cap3));
		pcd->ep0_req->dwc_req.length = UGETW(bos.wTotalLength) < len ?
					       UGETW(bos.wTotalLength) : len;
		pcd->ep0_status_pending = 1;
		pcd->ep0_req->dwc_req.buf[0] = pcd->ep0_status_buf;
		pcd->ep0_req->dwc_req.bufdma[0] = pcd->ep0_status_buf_dma;
		pcd->ep0_req->dwc_req.actual = 0;
		dwc_usb3_ep0_start_transfer(pcd, pcd->ep0_req);
		break;
#endif
	default:
		/* Call the Gadget Driver's setup functions */
		do_gadget_setup(pcd, &ctrl);
	}
}

/**
 * This function processes the GET_STATUS Setup Commands.
 */
static void do_get_status(dwc_usb3_pcd_t *pcd)
{
	usb_device_request_t ctrl = pcd->ep0_setup_pkt->req;
	uint8_t *status = pcd->ep0_status_buf;
	dwc_usb3_pcd_ep_t *ep;

#ifdef DEBUG_EP0
	dwc_debug(pcd->usb3_dev, "GET_STATUS %02x.%02x v%04x i%04x l%04x\n",
		  ctrl.bmRequestType, ctrl.bRequest, UGETW(ctrl.wValue),
		  UGETW(ctrl.wIndex), UGETW(ctrl.wLength));
#endif

	if (UGETW(ctrl.wLength) != 2
#ifdef CONFIG_USB_OTG_DWC
	    && UGETW(ctrl.wIndex) != 0xf000
#endif
	) {
		ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
		return;
	}

	switch (UT_GET_RECIPIENT(ctrl.bmRequestType)) {
	case UT_DEVICE:
#ifdef CONFIG_USB_OTG_DWC
		/* HNP Polling */
		if (UGETW(ctrl.wIndex) == 0xf000)
			*status = pcd->wants_host ? 1 : 0;
		else
#endif
		{
			*status = 1; /* Self powered */

			if (pcd->speed == USB_SPEED_SUPER) {
				if (pcd->state == DWC_STATE_CONFIGURED) {
					if (dwc_usb3_u1_enabled(pcd))
						*status |= 1 << 2;

					if (dwc_usb3_u2_enabled(pcd))
						*status |= 1 << 3;

					*status |= pcd->ltm_enable << 4;
				}
			} else {
				*status |= pcd->remote_wakeup_enable << 1;
			}
		}

		dwc_debug(pcd->usb3_dev, "GET_STATUS(Device)=%02x\n", *status);
		*(status + 1) = 0;
		break;

	case UT_INTERFACE:
		*status = 0;
		if (pcd->usb3_dev->core_params->wakeup)
			*status |= 1;
		*status |= pcd->remote_wakeup_enable << 1;
		dwc_debug(pcd->usb3_dev, "GET_STATUS(Interface %d)=%02x\n",
			  UGETW(ctrl.wIndex), *status);
		*(status + 1) = 0;
		break;

	case UT_ENDPOINT:
		ep = dwc_usb3_get_ep_by_addr(pcd, UGETW(ctrl.wIndex));

		/* @todo check for EP stall */
		*status = ep->dwc_ep.stopped;
		dwc_debug(pcd->usb3_dev, "GET_STATUS(Endpoint %d)=%02x\n",
			  UGETW(ctrl.wIndex), *status);
		*(status + 1) = 0;
		break;

	default:
		ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
		return;
	}

	pcd->ep0_status_pending = 1;
	pcd->ep0_req->dwc_req.buf[0] = status;
	pcd->ep0_req->dwc_req.bufdma[0] = pcd->ep0_status_buf_dma;
	pcd->ep0_req->dwc_req.length = 2;

#ifdef CONFIG_USB_OTG_DWC
	if (UGETW(ctrl.wIndex) == 0xf000)
		pcd->ep0_req->dwc_req.length = 1;
#endif
	pcd->ep0_req->dwc_req.actual = 0;
	dwc_usb3_ep0_start_transfer(pcd, pcd->ep0_req);
}

#ifdef CONFIG_USB_OTG_DWC
void dwc_usb3_start_hnp(dwc_usb3_pcd_t *pcd)
{
	struct otg_transceiver *otg = otg_get_transceiver();

	if (!otg)
		return;
	otg_start_hnp(otg);
	otg_put_transceiver(otg);
}

void dwc_usb3_host_release(dwc_usb3_pcd_t *pcd)
{
	struct otg_transceiver *otg = otg_get_transceiver();

	if (!otg)
		return;
	otg->host_release(otg);
	otg_put_transceiver(otg);
}
#endif

/**
 * This function processes the SET_FEATURE Setup Commands.
 */
static void do_set_feature(dwc_usb3_pcd_t *pcd)
{
	usb_device_request_t ctrl = pcd->ep0_setup_pkt->req;
	dwc_usb3_pcd_ep_t *ep;

#ifdef DEBUG_EP0
	dwc_debug(pcd->usb3_dev, "SET_FEATURE %02x.%02x v%04x i%04x l%04x\n",
		  ctrl.bmRequestType, ctrl.bRequest, UGETW(ctrl.wValue),
		  UGETW(ctrl.wIndex), UGETW(ctrl.wLength));
#endif

	switch (UT_GET_RECIPIENT(ctrl.bmRequestType)) {
	case UT_DEVICE:
		switch (UGETW(ctrl.wValue)) {
		case UF_DEVICE_REMOTE_WAKEUP:
			pcd->remote_wakeup_enable = 1;
			break;

		case UF_TEST_MODE:
			/* Setup the Test Mode tasklet to do the Test
			 * Packet generation after the SETUP Status
			 * phase has completed. */

			/* @todo This has not been tested since the
			 * tasklet struct was put into the PCD struct! */
			pcd->test_mode = UGETW(ctrl.wIndex) >> 8;
#if 1//def FIXME
			dwc_usb3_os_task_schedule(&pcd->test_mode_tasklet);
#endif
			break;

		case UF_DEVICE_B_HNP_ENABLE:
			dwc_debug(pcd->usb3_dev,
				  "SET_FEATURE: USB_DEVICE_B_HNP_ENABLE\n");
#ifdef CONFIG_USB_OTG_DWC
			if (pcd->wants_host) {
				pcd->b_hnp_enable = 0;
				pcd->wants_host = 0;
				dwc_usb3_start_hnp(pcd);
			} else {
				pcd->b_hnp_enable = 1;
			}
#endif
			break;

		case UOTG_NTF_HOST_REL:
			dwc_debug(pcd->usb3_dev,
				  "SET_FEATURE: USB_NTF_HOST_REL\n");
#ifdef CONFIG_USB_OTG_DWC
			dwc_usb3_host_release(pcd);
#endif
			break;

		case UOTG_B3_RSP_ENABLE:
			dwc_debug(pcd->usb3_dev,
				  "SET_FEATURE: USB_B3_RSP_ENABLE\n");
			break;

		case UF_DEVICE_A_HNP_SUPPORT:
			/* RH port supports HNP */
			dwc_debug(pcd->usb3_dev,
				  "SET_FEATURE: USB_DEVICE_A_HNP_SUPPORT\n");
			break;

		case UF_DEVICE_A_ALT_HNP_SUPPORT:
			/* other RH port does */
			dwc_debug(pcd->usb3_dev,
				"SET_FEATURE: USB_DEVICE_A_ALT_HNP_SUPPORT\n");
			break;

		case UF_U1_ENABLE:
			dwc_debug(pcd->usb3_dev, "SET_FEATURE: UF_U1_ENABLE\n");
			if (pcd->speed != USB_SPEED_SUPER ||
			    pcd->state != DWC_STATE_CONFIGURED ||
			    !(pcd->usb3_dev->core_params->pwrctl & 1)) {
				ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
				return;
			}
			dwc_usb3_enable_u1(pcd);
			break;

		case UF_U2_ENABLE:
			dwc_debug(pcd->usb3_dev, "SET_FEATURE: UF_U2_ENABLE\n");
			if (pcd->speed != USB_SPEED_SUPER ||
			    pcd->state != DWC_STATE_CONFIGURED ||
			    !(pcd->usb3_dev->core_params->pwrctl & 2)) {
				ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
				return;
			}
			dwc_usb3_enable_u2(pcd);
			break;

		case UF_LTM_ENABLE:
			dwc_debug(pcd->usb3_dev,
				  "SET_FEATURE: UF_LTM_ENABLE\n");
			if (pcd->speed != USB_SPEED_SUPER ||
			    pcd->state != DWC_STATE_CONFIGURED ||
			    UGETW(ctrl.wIndex) != 0) {
				ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
				return;
			}
			pcd->ltm_enable = 1;
			break;

		default:
			ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
			return;
		}
		break;

	case UT_INTERFACE:
		/* if FUNCTION_SUSPEND ... */
		if (UGETW(ctrl.wValue) == 0) {
			/* if Function Remote Wake Enabled ... */
			if ((UGETW(ctrl.wIndex) >> 8) & 2)
				pcd->remote_wakeup_enable = 1;
			else
				pcd->remote_wakeup_enable = 0;

			/* if Function Low Power Suspend ... */
			// TODO

			break;
		}
		do_gadget_setup(pcd, &ctrl);
		break;

	case UT_ENDPOINT:
		ep = dwc_usb3_get_ep_by_addr(pcd, UGETW(ctrl.wIndex));
		if (UGETW(ctrl.wValue) != UF_ENDPOINT_HALT) {
			ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
			return;
		}
		ep->dwc_ep.stopped = 1;
		dwc_usb3_ep_set_stall(pcd, ep);
		break;
	}

	pcd->ep0->dwc_ep.is_in = 1;
	pcd->ep0state = EP0_IN_WAIT_NRDY;
}

/**
 * This function processes the CLEAR_FEATURE Setup Commands.
 */
static void do_clear_feature(dwc_usb3_pcd_t *pcd)
{
	usb_device_request_t ctrl = pcd->ep0_setup_pkt->req;
	dwc_usb3_pcd_ep_t *ep;

#ifdef DEBUG_EP0
	dwc_debug(pcd->usb3_dev, "CLEAR_FEATURE %02x.%02x v%04x i%04x l%04x\n",
		  ctrl.bmRequestType, ctrl.bRequest, UGETW(ctrl.wValue),
		  UGETW(ctrl.wIndex), UGETW(ctrl.wLength));
#endif

	switch (UT_GET_RECIPIENT(ctrl.bmRequestType)) {
	case UT_DEVICE:
		switch (UGETW(ctrl.wValue)) {
		case UF_DEVICE_REMOTE_WAKEUP:
			pcd->remote_wakeup_enable = 0;
			break;

		case UF_TEST_MODE:
			/* @todo Add CLEAR_FEATURE for TEST modes. */
			break;

		case UF_U1_ENABLE:
			dwc_debug(pcd->usb3_dev,
				  "CLEAR_FEATURE: UF_U1_ENABLE\n");
			if (pcd->speed != USB_SPEED_SUPER ||
			    pcd->state != DWC_STATE_CONFIGURED) {
				ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
				return;
			}
			dwc_usb3_disable_u1(pcd);
			break;

		case UF_U2_ENABLE:
			dwc_debug(pcd->usb3_dev,
				  "CLEAR_FEATURE: UF_U2_ENABLE\n");
			if (pcd->speed != USB_SPEED_SUPER ||
			    pcd->state != DWC_STATE_CONFIGURED) {
				ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
				return;
			}
			dwc_usb3_disable_u2(pcd);
			break;

		case UF_LTM_ENABLE:
			dwc_debug(pcd->usb3_dev,
				  "CLEAR_FEATURE: UF_LTM_ENABLE\n");
			if (pcd->speed != USB_SPEED_SUPER ||
			    pcd->state != DWC_STATE_CONFIGURED ||
			    UGETW(ctrl.wIndex) != 0) {
				ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
				return;
			}
			pcd->ltm_enable = 0;
			break;

		default:
			ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
			return;
		}
		break;

	case UT_INTERFACE:
		/* if FUNCTION_SUSPEND ... */
		if (UGETW(ctrl.wValue) == 0) {
			/* if Function Remote Wake Enabled ... */
			if ((UGETW(ctrl.wIndex) >> 8) & 2) {
				pcd->remote_wakeup_enable = 0;
			}

			/* if Function Low Power Suspend ... */
			// TODO

			break;
		}
		ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
		return;

	case UT_ENDPOINT:
		ep = dwc_usb3_get_ep_by_addr(pcd, UGETW(ctrl.wIndex));
		if (UGETW(ctrl.wValue) != UF_ENDPOINT_HALT) {
			ep0_do_stall(pcd, -DWC_E_NOT_SUPPORTED);
			return;
		}
		do_clear_halt(pcd, ep);
		break;
	}

	pcd->ep0->dwc_ep.is_in = 1;
	pcd->ep0state = EP0_IN_WAIT_NRDY;
}

/**
 * This function processes SETUP commands. In Linux, the USB Command
 * processing is done in two places - the first being the PCD and the
 * second being the Gadget Driver (for example, the File-Backed Storage
 * Gadget Driver).
 *
 * <table>
 * <tr><td> Command </td><td> Driver </td><td> Description </td></tr>
 *
 * <tr><td> GET_STATUS </td><td> PCD </td><td> Command is processed
 * as defined in chapter 9 of the USB 2.0 Specification. </td></tr>
 *
 * <tr><td> SET_FEATURE </td><td> PCD / Gadget Driver </td><td> Device
 * and Endpoint requests are processed by the PCD. Interface requests
 * are passed to the Gadget Driver. </td></tr>
 *
 * <tr><td> CLEAR_FEATURE </td><td> PCD </td><td> Device and Endpoint
 * requests are processed by the PCD. Interface requests are ignored.
 * The only Endpoint feature handled is ENDPOINT_HALT. </td></tr>
 *
 * <tr><td> SET_ADDRESS </td><td> PCD </td><td> Program the DCFG register
 * with device address received. </td></tr>
 *
 * <tr><td> GET_DESCRIPTOR </td><td> Gadget Driver </td><td> Return the
 * requested descriptor. </td></tr>
 *
 * <tr><td> SET_DESCRIPTOR </td><td> Gadget Driver </td><td> Optional -
 * not implemented by any of the existing Gadget Drivers. </td></tr>
 *
 * <tr><td> GET_CONFIGURATION </td><td> Gadget Driver </td><td> Return
 * the current configuration. </td></tr>
 *
 * <tr><td> SET_CONFIGURATION </td><td> Gadget Driver </td><td> Disable
 * all EPs and enable EPs for new configuration. </td></tr>
 *
 * <tr><td> GET_INTERFACE </td><td> Gadget Driver </td><td> Return the
 * current interface. </td></tr>
 *
 * <tr><td> SET_INTERFACE </td><td> Gadget Driver </td><td> Disable all
 * EPs and enable EPs for new interface. </td></tr>
 * </table>
 *
 * When the SETUP Phase Done interrupt occurs, the PCD SETUP commands are
 * processed by do_setup. Calling the Function Driver's setup function from
 * pcd_setup processes the gadget SETUP commands.
 */
static void do_setup(dwc_usb3_pcd_t *pcd)
{
	usb_device_request_t ctrl = pcd->ep0_setup_pkt->req;
	dwc_usb3_pcd_ep_t *ep0 = pcd->ep0;
	uint16_t wvalue, wlength;
	int ret;

	dwc_debug(pcd->usb3_dev, "%s(%p)\n", __func__, pcd);
	wvalue = UGETW(ctrl.wValue);
	wlength = UGETW(ctrl.wLength);

#ifdef DEBUG_EP0
	dwc_debug(pcd->usb3_dev, "\n");
	dwc_debug(pcd->usb3_dev, "\n");
	dwc_print1(pcd->usb3_dev, "setup_pkt[0]=0x%08x\n",
		   pcd->ep0_setup_pkt->d32[0]);
	dwc_print1(pcd->usb3_dev, "setup_pkt[1]=0x%08x\n",
		   pcd->ep0_setup_pkt->d32[1]);
	dwc_print5(pcd->usb3_dev, "SETUP %02x.%02x v%04x i%04x l%04x\n",
		   ctrl.bmRequestType, ctrl.bRequest, wvalue,
		   UGETW(ctrl.wIndex), wlength);
	dwc_debug(pcd->usb3_dev, "\n");
#endif

	/* Clean up the request queue */
	dwc_usb3_os_request_nuke(pcd, ep0);
	ep0->dwc_ep.stopped = 0;
	ep0->dwc_ep.three_stage = 1;

	if (ctrl.bmRequestType & UE_DIR_IN) {
		ep0->dwc_ep.is_in = 1;
		pcd->ep0state = EP0_IN_DATA_PHASE;
	} else {
		ep0->dwc_ep.is_in = 0;
		pcd->ep0state = EP0_OUT_DATA_PHASE;
	}

	if (wlength == 0) {
		ep0->dwc_ep.is_in = 1;
		pcd->ep0state = EP0_IN_WAIT_GADGET;
		ep0->dwc_ep.three_stage = 0;
	}

	if ((UT_GET_TYPE(ctrl.bmRequestType)) != UT_STANDARD) {
		/* handle non-standard (class/vendor) requests
		 * in the gadget driver
		 */
		do_gadget_setup(pcd, &ctrl);
		return;
	}

	/* @todo NGS: Handle bad setup packet? */

///////////////////////////////////////////
//// --- Standard Request handling --- ////

	switch (ctrl.bRequest) {
	case UR_GET_STATUS:
		do_get_status(pcd);
		break;

	case UR_CLEAR_FEATURE:
		do_clear_feature(pcd);
		break;

	case UR_SET_FEATURE:
		do_set_feature(pcd);
		break;

	case UR_SET_ADDRESS:
		do_set_address(pcd);
		break;

	case UR_SET_INTERFACE:
		dwc_debug(pcd->usb3_dev, "USB_REQ_SET_INTERFACE\n");
		dwc_usb3_clr_eps_enabled(pcd);

#ifdef DWC_STAR_9000463548_WORKAROUND
		pcd->configuring = 1;
#endif
		ret = do_gadget_setup(pcd, &ctrl);

#ifdef DWC_STAR_9000463548_WORKAROUND
		if (ret < 0)
			pcd->configuring = 0;
#endif
		break;

	case UR_SET_CONFIG:
		dwc_debug(pcd->usb3_dev, "USB_REQ_SET_CONFIGURATION\n");
#ifdef DWC_UTE
		if (wvalue != 0)
			dwc_usb3_ute_config(pcd->usb3_dev);
#endif
		dwc_usb3_clr_eps_enabled(pcd);

#ifdef DWC_STAR_9000463548_WORKAROUND
		pcd->configuring = 1;
#endif
		ret = do_gadget_setup(pcd, &ctrl);
		if (ret >= 0) {
			if (wvalue != 0)
				pcd->state = DWC_STATE_CONFIGURED;
			else
				pcd->state = DWC_STATE_ADDRESSED;
		}

#ifdef DWC_STAR_9000463548_WORKAROUND
		else
			pcd->configuring = 0;
#endif
		/* Must wait until SetConfig before accepting U1/U2 link
		 * control, otherwise we have problems with VIA hubs
		 */
		if (pcd->usb3_dev->core_params->pwrctl & 1)
			dwc_usb3_accept_u1(pcd);
		if (pcd->usb3_dev->core_params->pwrctl & 2)
			dwc_usb3_accept_u2(pcd);

		pcd->ltm_enable = 0;
		break;

	case UR_SYNCH_FRAME:
		do_gadget_setup(pcd, &ctrl);
		break;

	case UR_GET_DESCRIPTOR:
		do_get_descriptor(pcd);
		break;

	case UR_SET_SEL:
		/* For now this is a no-op */
		pcd->ep0_status_pending = 1;
		pcd->ep0_req->dwc_req.buf[0] = pcd->ep0_status_buf;
		pcd->ep0_req->dwc_req.bufdma[0] = pcd->ep0_status_buf_dma;
		pcd->ep0_req->dwc_req.length = DWC_STATUS_BUF_SIZE;
		pcd->ep0_req->dwc_req.actual = 0;
		ep0->dwc_ep.send_zlp = 0;
		dwc_usb3_ep0_start_transfer(pcd, pcd->ep0_req);
		break;

	case UR_SET_ISOC_DELAY:
		/* For now this is a no-op */
		pcd->ep0->dwc_ep.is_in = 1;
		pcd->ep0state = EP0_IN_WAIT_NRDY;
		break;

	default:
		/* Call the Gadget Driver's setup functions */
		do_gadget_setup(pcd, &ctrl);
		break;
	}
}


/*========================================================================*/
/*
 * OS-specific transfer functions
 *
 * These contain things specific to the OS, such as (in Linux) the queue of
 * requests and the DMA descriptors for each EP.
 */

/**
 * This function gets the DMA descriptor (TRB) for a data transfer and stores
 * it in the given request. Called from the core code.
 *
 * @param pcd    Programming view of DWC_usb3 peripheral controller.
 * @param ep     The EP for the transfer.
 * @param req    The request that needs the TRB.
 */
void dwc_usb3_os_get_trb(dwc_usb3_pcd_t *pcd, dwc_usb3_pcd_ep_t *ep,
			 dwc_usb3_pcd_req_t *req)
{
	dwc_usb3_dma_desc_t *desc;
	dma_addr_t desc_dma;

	dwc_debug(pcd->usb3_dev, "%s()\n", __func__);

	/* If EP0, fill request with EP0 IN/OUT data TRB */
	if (ep == pcd->ep0) {
		if (ep->dwc_ep.is_in) {
			req->dwc_req.trb = pcd->ep0_in_desc;
			req->dwc_req.trbdma = pcd->ep0_in_desc_dma;
		} else {
			req->dwc_req.trb = pcd->ep0_out_desc;
			req->dwc_req.trbdma = pcd->ep0_out_desc_dma;
		}

	/* Else fill request with TRB from the non-EP0 allocation */
	} else if (!req->dwc_req.trb) {
		/* Get the next DMA Descriptor (TRB) for this EP */
		desc = (dwc_usb3_dma_desc_t *)
			(ep->dwc_ep.dma_desc + ep->dwc_ep.desc_idx
						* req->dwc_req.numbuf * 16);
		desc_dma = (dma_addr_t)
			((unsigned long)ep->dwc_ep.dma_desc_dma +
			 (unsigned long)ep->dwc_ep.desc_idx
						* req->dwc_req.numbuf * 16);

		if (++ep->dwc_ep.desc_idx >= ep->dwc_ep.num_desc)
			ep->dwc_ep.desc_idx = 0;
		ep->dwc_ep.desc_avail--;

		req->dwc_req.trb = desc;
		req->dwc_req.trbdma = desc_dma;
	}
}

/**
 * This function handles EP0 transfers.
 *
 * This function gets the request corresponding to the current EP0 transfer.
 * If EP0 is in IDLE state, it calls the Linux-specific do_setup() function to
 * begin handling the next SETUP request, otherwise it calls the core function
 * for handling the next stage of the transfer.
 */
void dwc_usb3_os_handle_ep0(dwc_usb3_pcd_t *pcd, uint32_t event)
{
	dwc_usb3_pcd_ep_t *ep0 = pcd->ep0;
	dwc_usb3_pcd_req_t *req = NULL;

#ifdef DEBUG_EP0
	dwc_debug(pcd->usb3_dev, "%s()\n", __func__);
#endif
	if (!list_empty(&ep0->dwc_ep.queue))
		req = list_first_entry(&ep0->dwc_ep.queue, dwc_usb3_pcd_req_t,entry);

	if (pcd->ep0state == EP0_IDLE) {
#ifdef DEBUG_EP0
		dwc_usb3_print_ep0_state(pcd);
		dwc_debug(pcd->usb3_dev, "IDLE EP%d-%s\n", ep0->dwc_ep.num,
			  (ep0->dwc_ep.is_in ? "IN" : "OUT"));
#endif
		pcd->request_config = 0;
		do_setup(pcd);
	} else {
		dwc_usb3_handle_ep0(pcd, req, event);
	}
}

/*
 * This function marks all requests in an EP queue as not started.
 */
void dwc_usb3_mark_ep_queue_not_started(dwc_usb3_pcd_t *pcd,
					dwc_usb3_pcd_ep_t *ep)
{
	dwc_usb3_pcd_req_t *req;
	struct list_head *list_item;

	list_for_each (list_item, &ep->dwc_ep.queue) {
		req = list_entry(list_item, dwc_usb3_pcd_req_t, entry);
		if (req->dwc_req.flags & DWC_PCD_REQ_STARTED)
			req->dwc_req.flags &= ~DWC_PCD_REQ_STARTED;
	}
}

/**
 * This function handles non-EP0 transfers.
 *
 * This function gets the request corresponding to the completed transfer
 * and then calls the core function for handling the completion.
 */
void dwc_usb3_os_complete_request(dwc_usb3_pcd_t *pcd, dwc_usb3_pcd_ep_t *ep,
				  uint32_t event)
{
	dwc_usb3_pcd_req_t *req;
	dwc_usb3_dma_desc_t *desc;
	int ret;

	dwc_debug(pcd->usb3_dev, "%s()\n", __func__);
	dwc_debug(pcd->usb3_dev, "Requests %d\n", pcd->request_pending);

	/* Check for a pending request */
	if (list_empty(&ep->dwc_ep.queue)) {
		dwc_print(pcd->usb3_dev, "%s(%p), ep->dwc_ep.queue empty!\n",
			  __func__, ep);
		return;
	}

	req = list_first_entry(&ep->dwc_ep.queue, dwc_usb3_pcd_req_t, entry);
next:
	ret = dwc_usb3_ep_complete_request(pcd, ep, req, event);
	if (!ret)
		return;

	if (list_empty(&ep->dwc_ep.queue))
		return;

	if (ret < 0) {
		/* Isoc restart - mark all requests in queue as not started */
		dwc_usb3_mark_ep_queue_not_started(pcd, ep);
	} else {
		/* ep_complete_request() wants to process next TRB */
		req = list_first_entry(&ep->dwc_ep.queue, dwc_usb3_pcd_req_t,
				       entry);
		dwc_debug(pcd->usb3_dev, "Requests2 %d\n",
			  pcd->request_pending);
		desc = req->dwc_req.trb;
		if (desc && (req->dwc_req.flags & DWC_PCD_REQ_STARTED) &&
		    !dwc_usb3_is_hwo(desc)) {
			dwc_isocdbg(pcd->usb3_dev, "Processing next TRB\n");
			goto next;
		}
	}
}

/**
 * This function checks the EP request queue, if the queue is not empty then
 * the next transfer is started.
 *
 * @param pcd Programming view of DWC_usb3 peripheral controller.
 * @param ep  EP to operate on.
 */
void dwc_usb3_os_start_next_request(dwc_usb3_pcd_t *pcd, dwc_usb3_pcd_ep_t *ep)
{
	dwc_usb3_pcd_req_t *req = NULL;
	struct list_head *list_item;

	dwc_debug(pcd->usb3_dev, "%s(%p)\n", __func__, ep);

	if (list_empty(&ep->dwc_ep.queue)) {
		dwc_debug(pcd->usb3_dev, "start_next EP%d-%s: queue empty\n",
			  ep->dwc_ep.num, ep->dwc_ep.is_in ? "IN" : "OUT");
		return;
	}

	list_for_each (list_item, &ep->dwc_ep.queue) {
		req = list_entry(list_item, dwc_usb3_pcd_req_t, entry);
		if (!(req->dwc_req.flags & DWC_PCD_REQ_STARTED)) {
			if (ep->dwc_ep.desc_avail <= 0) {
				dwc_debug(pcd->usb3_dev,
					  "start_next EP%d-%s: no TRB avail\n",
					  ep->dwc_ep.num, ep->dwc_ep.is_in ?
					  "IN" : "OUT");
				return;
			}

			dwc_debug(pcd->usb3_dev, "start_next EP%d-%s: OK\n",
				  ep->dwc_ep.num,
				  ep->dwc_ep.is_in ? "IN" : "OUT");

			/* Setup and start the Transfer */
			//dwc_debug(pcd->usb3_dev,
			//	  " -> starting xfer (start_next_req) %s %s\n",
			//	  ep->dwc_ep.ep.name,
			//	  ep->dwc_ep.is_in ? "IN" : "OUT");
			dwc_usb3_ep_start_transfer(pcd, ep, req, 0);
			return;
		}
	}

	dwc_debug(pcd->usb3_dev, "start_next EP%d-%s: all req active\n",
		  ep->dwc_ep.num, ep->dwc_ep.is_in ? "IN" : "OUT");
}

/**
 * Start an Isoc EP running at the proper interval, after receiving the initial
 * XferNRdy event.
 */
void dwc_usb3_os_isoc_ep_start(dwc_usb3_pcd_t *pcd, dwc_usb3_pcd_ep_t *ep,
			       uint32_t event)
{
	dwc_usb3_pcd_req_t *req = NULL;
	dwc_usb3_dev_ep_regs_t __iomem *ep_reg;
	struct list_head *list_item;
	dwc_usb3_dma_desc_t *desc;
	dma_addr_t desc_dma;
	uint8_t *tri;
	int owned;

	dwc_debug(pcd->usb3_dev, "%s(%p,%p,%x)\n", __func__, pcd, ep, event);
	dwc_isocdbg(pcd->usb3_dev, "%s(%08x)\n", __func__, event);

	if (list_empty(&ep->dwc_ep.queue)) {
		dwc_print(pcd->usb3_dev, "%s(%p), ep->dwc_ep.queue empty!\n",
			  __func__, ep);
		return;
	}

	if (ep->dwc_ep.desc_avail <= 0) {
		dwc_print(pcd->usb3_dev, "EP%d-%s: no TRB avail!\n",
			  ep->dwc_ep.num, ep->dwc_ep.is_in ? "IN" : "OUT");
		return;
	}

	/* Need to restart after hibernation? */
	owned = ep->dwc_ep.hiber_desc_idx - 1;
	if (owned < 0)
		goto nohiber;

	/* For restart after hibernation, we need to restart the transfer with
	 * the address of the TRB that was last active before the hibernation.
	 * That address was saved in 'hiber_desc_idx' by the hibernation
	 * wakeup code.
	 */
	if (ep->dwc_ep.is_in) {
		ep_reg = ep->dwc_ep.in_ep_reg;
		tri = &ep->dwc_ep.tri_in;
	} else {
		ep_reg = ep->dwc_ep.out_ep_reg;
		tri = &ep->dwc_ep.tri_out;
	}

	dwc_debug0(pcd->usb3_dev, "Restarting Isoc xfer\n");
	desc = (dwc_usb3_dma_desc_t *)(ep->dwc_ep.dma_desc + owned * 16);
	desc_dma = (dma_addr_t)
		((unsigned long)ep->dwc_ep.dma_desc_dma + owned * 16);
	dwc_debug1(pcd->usb3_dev, "desc=%08lx\n", (unsigned long)desc);

#ifdef VERBOSE
	dwc_debug5(pcd->usb3_dev, "%08x %08x %08x %08x (%08x)\n",
		   *((unsigned *)desc), *((unsigned *)desc + 1),
		   *((unsigned *)desc + 2), *((unsigned *)desc + 3),
		   (unsigned)desc_dma);
#endif
	*tri = dwc_usb3_dep_startxfer(pcd, ep_reg, desc_dma, 0);
	return;

nohiber:
	/*
	 * Start the next queued transfer at the target uFrame
	 */

	list_for_each (list_item, &ep->dwc_ep.queue) {
		req = list_entry(list_item, dwc_usb3_pcd_req_t, entry);
		if (req->dwc_req.flags & DWC_PCD_REQ_STARTED)
			req = NULL;
		else
			break;
	}

	dwc_debug(pcd->usb3_dev, "req=%p\n", req);
	if (!req) {
		dwc_print(pcd->usb3_dev, "EP%d-%s: no requests to start!\n",
			  ep->dwc_ep.num, ep->dwc_ep.is_in ? "IN" : "OUT");
		return;
	}

	dwc_usb3_ep_start_transfer(pcd, ep, req, event);

	/*
	 * Now start any remaining queued transfers
	 */

	while (!list_is_last(list_item, &ep->dwc_ep.queue)) {
		list_item = list_item->next;
		req = list_entry(list_item, dwc_usb3_pcd_req_t, entry);
		if (!(req->dwc_req.flags & DWC_PCD_REQ_STARTED)) {
			if (ep->dwc_ep.desc_avail <= 0) {
				dwc_print(pcd->usb3_dev,
					  "start_next EP%d-%s: no TRB avail!\n",
					  ep->dwc_ep.num, ep->dwc_ep.is_in ?
					  "IN" : "OUT");
				return;
			}

			dwc_usb3_ep_start_transfer(pcd, ep, req, 0);
		}
	}
}

/**
 * This function terminates all the requests in the EP request queue.
 */
void dwc_usb3_os_request_nuke(dwc_usb3_pcd_t *pcd, dwc_usb3_pcd_ep_t *ep)
{
	dwc_usb3_pcd_req_t *req;

	dwc_debug(pcd->usb3_dev, "%s(%p)\n", __func__, ep);
	ep->dwc_ep.stopped = 1;

	/* called with irqs blocked?? */
	while (!list_empty(&ep->dwc_ep.queue)) {
		req = list_first_entry(&ep->dwc_ep.queue, dwc_usb3_pcd_req_t,
				       entry);
		dwc_usb3_request_done(pcd, ep, req, -DWC_E_SHUTDOWN);
	}
}

void dwc_usb3_os_task_schedule(struct tasklet_struct *tasklet)
{
	tasklet_schedule(tasklet);
}


/*========================================================================*/
/*
 * Linux Gadget API related functions
 */

/* Gadget wrapper */

static struct gadget_wrapper {
	dwc_usb3_pcd_t *pcd;

	char ep_names[DWC_MAX_EPS * 2 - 1][16];

	struct usb_gadget gadget;
	struct usb_gadget_driver *driver;

	dwc_usb3_pcd_req_t ep0_req;

	dwc_usb3_pcd_ep_t ep0;
	dwc_usb3_pcd_ep_t out_ep[DWC_MAX_EPS - 1];
	dwc_usb3_pcd_ep_t in_ep[DWC_MAX_EPS - 1];
} *gadget_wrapper[FSTOR_NUM_GADGETS];


/**
 * Passes a Setup request to the Gadget driver
 */
int dwc_usb3_pcd_setup(dwc_usb3_pcd_t *pcd, void *data)
{
	int retval = -DWC_E_NOT_SUPPORTED;
	unsigned devnum = pcd->devnum;

	dwc_debug(pcd->usb3_dev, "%s()\n", __func__);
	dwc_debug(pcd->usb3_dev, "devnum=%u\n", devnum);

	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		dwc_warn(pcd->usb3_dev, "%s, bad devnum %u or null wrapper!\n",
			 __func__, devnum);
		return -DWC_E_INVALID;
	}

	if (gadget_wrapper[devnum]->driver &&
	    gadget_wrapper[devnum]->driver->setup) {
		spin_unlock(&pcd->lock);
		retval = gadget_wrapper[devnum]->driver->setup(
					&gadget_wrapper[devnum]->gadget,
					(struct usb_ctrlrequest *)data);
		spin_lock(&pcd->lock);
		if (retval == -ENOTSUPP)
			retval = -DWC_E_NOT_SUPPORTED;
		else if (retval < 0)
			retval = -DWC_E_INVALID;
	}

	return retval;
}

/**
 * Passes a Disconnect event to the Gadget driver
 */
int dwc_usb3_pcd_disconnect(dwc_usb3_pcd_t *pcd)
{
	unsigned devnum = pcd->devnum;

	dwc_debug(pcd->usb3_dev, "%s()\n", __func__);
	dwc_debug(pcd->usb3_dev, "devnum=%u\n", devnum);

	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		dwc_warn(pcd->usb3_dev, "%s, bad devnum %u or null wrapper!\n",
			 __func__, devnum);
		return -DWC_E_INVALID;
	}

	if (gadget_wrapper[devnum]->driver &&
	    gadget_wrapper[devnum]->driver->disconnect) {
		spin_unlock(&pcd->lock);
		gadget_wrapper[devnum]->driver->disconnect(
				&gadget_wrapper[devnum]->gadget);
		spin_lock(&pcd->lock);
	}

	return 0;
}

/**
 * Sets the connection speed for the Gadget
 */
int dwc_usb3_pcd_set_speed(dwc_usb3_pcd_t *pcd, int speed)
{
	unsigned devnum = pcd->devnum;

	dwc_debug(pcd->usb3_dev, "%s()\n", __func__);
	dwc_debug(pcd->usb3_dev, "devnum=%u\n", devnum);

	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		dwc_warn(pcd->usb3_dev, "%s, bad devnum %u or null wrapper!\n",
			 __func__, devnum);
		return -DWC_E_INVALID;
	}

	gadget_wrapper[devnum]->gadget.speed = speed;

	/* Set the MPS of EP0 based on the connection speed */
	switch (speed) {
	case USB_SPEED_SUPER:
		pcd->ep0->dwc_ep.maxpacket = 512;
		pcd->ep0->usb_ep.maxpacket = 512;
		break;

	case USB_SPEED_HIGH:
	case USB_SPEED_FULL:
		pcd->ep0->dwc_ep.maxpacket = 64;
		pcd->ep0->usb_ep.maxpacket = 64;
		break;

	case USB_SPEED_LOW:
		pcd->ep0->dwc_ep.maxpacket = 8;
		pcd->ep0->usb_ep.maxpacket = 8;
		break;
	}

	return 0;
}

/**
 * Passes a Suspend event to the Gadget driver
 */
int dwc_usb3_pcd_suspend(dwc_usb3_pcd_t *pcd)
{
	unsigned devnum = pcd->devnum;

	dwc_debug(pcd->usb3_dev, "%s()\n", __func__);
	dwc_debug(pcd->usb3_dev, "devnum=%u\n", devnum);

	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		dwc_warn(pcd->usb3_dev, "%s, bad devnum %u or null wrapper!\n",
			 __func__, devnum);
		return -DWC_E_INVALID;
	}

	if (gadget_wrapper[devnum]->driver &&
	    gadget_wrapper[devnum]->driver->suspend) {
		spin_unlock(&pcd->lock);
		gadget_wrapper[devnum]->driver->suspend(
				&gadget_wrapper[devnum]->gadget);
		spin_lock(&pcd->lock);
	}

	return 0;
}

/**
 * Passes a Resume event to the Gadget driver
 */
int dwc_usb3_pcd_resume(dwc_usb3_pcd_t *pcd)
{
	unsigned devnum = pcd->devnum;

	dwc_debug(pcd->usb3_dev, "%s()\n", __func__);
	dwc_debug(pcd->usb3_dev, "devnum=%u\n", devnum);

	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		dwc_warn(pcd->usb3_dev, "%s, bad devnum %u or null wrapper!\n",
			 __func__, devnum);
		return -DWC_E_INVALID;
	}

	if (gadget_wrapper[devnum]->driver &&
	    gadget_wrapper[devnum]->driver->resume) {
		spin_unlock(&pcd->lock);
		gadget_wrapper[devnum]->driver->resume(
				&gadget_wrapper[devnum]->gadget);
		spin_lock(&pcd->lock);
	}

	return 0;
}

#ifdef CONFIG_HITECH
static void map_buffers(struct pci_dev *dev, struct usb_request *usb_req,
			dwc_usb3_pcd_ep_t *pcd_ep, int *req_flags)
{
	if (dwc_usb3_pcd_ep_is_in(pcd_ep)) {
# ifdef DWC_TEST_ISOC_CHAIN
		if (dwc_usb3_pcd_ep_type(pcd_ep) == UE_ISOCHRONOUS) {
			if (usb_req->length > 58 && usb_req->buf2[0]) {
				usb_req->dma2[0] = pci_map_single(dev,
					usb_req->buf2[0], 29, PCI_DMA_TODEVICE);
			}
			if (usb_req->length > 58) {
				usb_req->dma = pci_map_single(dev, usb_req->buf,
					usb_req->length - 58, PCI_DMA_TODEVICE);
			} else {
				usb_req->dma = pci_map_single(dev, usb_req->buf,
					usb_req->length, PCI_DMA_TODEVICE);
			}
			if (usb_req->length > 58 && usb_req->buf2[1]) {
				usb_req->dma2[1] = pci_map_single(dev,
					usb_req->buf2[1], 29, PCI_DMA_TODEVICE);
			}
		} else
# endif
			usb_req->dma = pci_map_single(dev, usb_req->buf,
					usb_req->length, PCI_DMA_TODEVICE);
		*req_flags |= DWC_PCD_REQ_MAP_DMA | DWC_PCD_REQ_IN;
	} else {
# ifdef DWC_TEST_ISOC_CHAIN
		if (dwc_usb3_pcd_ep_type(pcd_ep) == UE_ISOCHRONOUS) {
			if (usb_req->length > 58 && usb_req->buf2[0]) {
				usb_req->dma2[0] = pci_map_single(dev,
				      usb_req->buf2[0], 29, PCI_DMA_FROMDEVICE);
			}
			if (usb_req->length > 58) {
				usb_req->dma = pci_map_single(dev, usb_req->buf,
				      usb_req->length - 58, PCI_DMA_FROMDEVICE);
			} else {
				usb_req->dma = pci_map_single(dev, usb_req->buf,
				      usb_req->length, PCI_DMA_FROMDEVICE);
			}
			if (usb_req->length > 58 && usb_req->buf2[1]) {
				usb_req->dma2[1] = pci_map_single(dev,
				      usb_req->buf2[1], 29, PCI_DMA_FROMDEVICE);
			}
		} else
# endif
			usb_req->dma = pci_map_single(dev, usb_req->buf,
					usb_req->length, PCI_DMA_FROMDEVICE);
		*req_flags |= DWC_PCD_REQ_MAP_DMA;
	}
}

static void unmap_buffers(struct pci_dev *dev, struct usb_request *usb_req,
			  dwc_usb3_pcd_ep_t *pcd_ep, int *req_flags)
{
	if (*req_flags & DWC_PCD_REQ_IN) {
# ifdef DWC_TEST_ISOC_CHAIN
		if (dwc_usb3_pcd_ep_type(pcd_ep) == UE_ISOCHRONOUS) {
			if (usb_req->dma2[1] != DWC_DMA_ADDR_INVALID) {
				pci_unmap_single(dev, usb_req->dma2[1], 29,
						 PCI_DMA_TODEVICE);
			}
			if (usb_req->dma2[1] != DWC_DMA_ADDR_INVALID ||
			    usb_req->dma2[0] != DWC_DMA_ADDR_INVALID) {
				pci_unmap_single(dev, usb_req->dma,
					usb_req->length - 58, PCI_DMA_TODEVICE);
			} else {
				pci_unmap_single(dev, usb_req->dma,
					usb_req->length, PCI_DMA_TODEVICE);
			}
			if (usb_req->dma2[0] != DWC_DMA_ADDR_INVALID) {
				pci_unmap_single(dev, usb_req->dma2[0], 29,
						 PCI_DMA_TODEVICE);
			}
		} else
# endif
			pci_unmap_single(dev, usb_req->dma, usb_req->length,
					 PCI_DMA_TODEVICE);
	} else {
# ifdef DWC_TEST_ISOC_CHAIN
		if (dwc_usb3_pcd_ep_type(pcd_ep) == UE_ISOCHRONOUS) {
			if (usb_req->dma2[1] != DWC_DMA_ADDR_INVALID) {
				pci_unmap_single(dev, usb_req->dma2[1], 29,
						 PCI_DMA_FROMDEVICE);
			}
			if (usb_req->dma2[1] != DWC_DMA_ADDR_INVALID ||
			    usb_req->dma2[0] != DWC_DMA_ADDR_INVALID) {
				pci_unmap_single(dev, usb_req->dma,
				      usb_req->length - 58, PCI_DMA_FROMDEVICE);
			} else {
				pci_unmap_single(dev, usb_req->dma,
				      usb_req->length, PCI_DMA_FROMDEVICE);
			}
			if (usb_req->dma2[0] != DWC_DMA_ADDR_INVALID) {
				pci_unmap_single(dev, usb_req->dma2[0], 29,
						 PCI_DMA_FROMDEVICE);
			}
		} else
# endif
			pci_unmap_single(dev, usb_req->dma, usb_req->length,
					 PCI_DMA_FROMDEVICE);
	}

	*req_flags &= ~(DWC_PCD_REQ_MAP_DMA | DWC_PCD_REQ_IN);

# ifdef DWC_TEST_ISOC_CHAIN
	usb_req->dma2[0] = DWC_DMA_ADDR_INVALID;
	usb_req->dma2[1] = DWC_DMA_ADDR_INVALID;
# endif
	usb_req->dma = DWC_DMA_ADDR_INVALID;
}
#endif

#if defined(DEBUG) || defined(ISOC_DEBUG)
static void dbg_databuf(dwc_usb3_pcd_t *pcd, struct usb_request *usb_req,
			dwc_usb3_pcd_ep_t *pcd_ep, uint32_t actual,
			dma_addr_t dma
#ifdef DWC_TEST_ISOC_CHAIN
		       , dma_addr_t dma2[2]
#endif
		      )
{
	if (dwc_usb3_pcd_ep_type(pcd_ep) == UE_ISOCHRONOUS) {
		unsigned char *buf = (unsigned char *)usb_req->buf;
# ifdef DWC_TEST_ISOC_CHAIN
		unsigned char *buf0 = (unsigned char *)usb_req->buf2[0];
		unsigned char *buf1 = (unsigned char *)usb_req->buf2[1];
# endif
		if (buf) {
			if (actual >= 4) {
# ifdef DWC_TEST_ISOC_CHAIN
				dwc_isocdbg(pcd->usb3_dev,
					"%1lx %1lx %1lx bdata: %02x %02x %02x "
					"%02x .. %02x %02x %02x %02x .. %02x "
					"%02x %02x %02x\n",
					(unsigned long)dma,
					(unsigned long)dma2[0],
					(unsigned long)dma2[1],
					buf[0], buf[1], buf[2], buf[3],
					buf0[0], buf0[1], buf0[2], buf0[3],
					buf1[0], buf1[1], buf1[2], buf1[3]);
# else
				dwc_isocdbg(pcd->usb3_dev,
					    "%1lx bdata: %02x %02x %02x %02x\n",
					    (unsigned long)dma,
					    buf[0], buf[1], buf[2], buf[3]);
# endif
			} else if (actual > 0) {
				dwc_isocdbg(pcd->usb3_dev,
					    "%1lx bdata: %02x ...\n",
					    (unsigned long)dma, buf[0]);
			} else {
				dwc_isocdbg(pcd->usb3_dev,
					    "%1lx bdata: 0-len!\n",
					    (unsigned long)dma);
			}
		} else {
			dwc_isocdbg(pcd->usb3_dev, "bdata: buf NULL!\n");
		}
	}
}
#endif

/**
 * Passes a Completion event to the Gadget driver
 */
int dwc_usb3_pcd_complete(dwc_usb3_pcd_t *pcd, dwc_usb3_pcd_ep_t *pcd_ep,
			  dwc_usb3_pcd_req_t *pcd_req, int32_t status)
{
	struct usb_ep *usb_ep = &pcd_ep->usb_ep;
	struct usb_request *usb_req = &pcd_req->usb_req;
	uint32_t actual = pcd_req->dwc_req.actual;
	int *req_flags = &pcd_req->dwc_req.flags;
#ifdef CONFIG_HITECH
	struct pci_dev *dev = pcd->usb3_dev->pcidev;
#endif
#ifdef CONFIG_IPMATE
	/* @todo Implement for IPMate */
#endif
	dma_addr_t dma;
#ifdef DWC_TEST_ISOC_CHAIN
	dma_addr_t dma2[2];
#endif

	/* Remove the request from the queue */
	list_del_init(&pcd_req->entry);

	/* Save DMA address for debug */
	dma = usb_req->dma;
#ifdef DWC_TEST_ISOC_CHAIN
	dma2[0] = usb_req->dma2[0];
	dma2[1] = usb_req->dma2[1];
#endif

	/* Unmap DMA */
	if (*req_flags & DWC_PCD_REQ_MAP_DMA) {
		dwc_debug(pcd->usb3_dev, "DMA unmap req %p\n", usb_req);
#ifdef CONFIG_HITECH
		unmap_buffers(dev, usb_req, pcd_ep, req_flags);
#endif
#ifdef CONFIG_IPMATE
		/* @todo Implement for IPMate */
#endif
	}

#if defined(DEBUG) || defined(ISOC_DEBUG)
	dbg_databuf(pcd, usb_req, pcd_ep, actual, dma
# ifdef DWC_TEST_ISOC_CHAIN
		    , dma2
# endif
		   );
#endif

	if (usb_req->complete) {
		switch (status) {
		case -DWC_E_SHUTDOWN:
			usb_req->status = -ESHUTDOWN;
			break;
		case -DWC_E_RESTART:
			usb_req->status = -ECONNRESET;
			break;
		case -DWC_E_INVALID:
			usb_req->status = -EINVAL;
			break;
		case -DWC_E_TIMEOUT:
			usb_req->status = -ETIMEDOUT;
			break;
		default:
			usb_req->status = status;
		}

		usb_req->actual = actual;
		spin_unlock(&pcd->lock);
		usb_req->complete(usb_ep, usb_req);
		spin_lock(&pcd->lock);
	}

	if (pcd->request_pending > 0)
		--pcd->request_pending;

	return 0;
}

/*
 * Gadget EP ops
 */

static char *trb_alloc(dwc_usb3_pcd_t *pcd, int type, int intvl, int num_trbs,
		       int desc_size, dma_addr_t *desc_dma_ret)
{
	dwc_usb3_dma_desc_t *cur_trb;
	dma_addr_t desc_dma;
	char *desc;
	int i;
#ifdef DWC_TEST_ISOC_CHAIN
	int j;
#endif
	dwc_debug(pcd->usb3_dev, "%s()\n", __func__);
	dwc_debug(pcd->usb3_dev, "size:%d trbs:%d\n", desc_size, num_trbs);

	desc = dma_alloc_coherent(NULL, desc_size, &desc_dma,
				  GFP_ATOMIC | GFP_DMA);
	if (!desc)
		return NULL;
	memset(desc, 0, desc_size);

	/* For Isoc EPs link the TRBs in a ring */
	if (type == UE_ISOCHRONOUS) {
		num_trbs--;
		cur_trb = (dwc_usb3_dma_desc_t *)desc;
		dwc_debug(pcd->usb3_dev, "start:%p\n", cur_trb);

		/* Do the transfer TRBs */
		for (i = 0; i < num_trbs; i++, cur_trb++) {
			/*
			 * For small intervals, only set the IOC bit in every
			 * 8th TRB, for interrupt moderation purposes
			 */
#ifdef DWC_ISOC_INTR_MODERATION
			if (intvl > 3 || (i & 7) == 7 || i == num_trbs - 1)
#endif
				dwc_usb3_fill_desc(cur_trb, 0, 0, 0,
						   DWC_DSCCTL_TRBCTL_ISOC_1ST,
						   DWC_DSCCTL_IOC_BIT |
						   DWC_DSCCTL_IMI_BIT |
						   DWC_DSCCTL_CSP_BIT, 0);
#ifdef DWC_ISOC_INTR_MODERATION
			else
				dwc_usb3_fill_desc(cur_trb, 0, 0, 0,
						   DWC_DSCCTL_TRBCTL_ISOC_1ST,
						   DWC_DSCCTL_IMI_BIT |
						   DWC_DSCCTL_CSP_BIT, 0);
#endif
#ifdef DWC_TEST_ISOC_CHAIN
			/* Add 2 more TRBs per entry, chain them to the 1st */
			dwc_usb3_start_desc_chain(cur_trb);
			cur_trb++;

			for (j = 0; j < 2; j++, cur_trb++)
				dwc_usb3_fill_desc(cur_trb, 0, 0, 0,
						   DWC_DSCCTL_TRBCTL_ISOC,
						   DWC_DSCCTL_IMI_BIT |
						   DWC_DSCCTL_CSP_BIT |
						   DWC_DSCCTL_CHN_BIT, 0);
			cur_trb--;
			dwc_usb3_end_desc_chain(cur_trb);
#endif
		}

		dwc_debug(pcd->usb3_dev, "end:%p\n", cur_trb);

		/* Now the link TRB */
		dwc_usb3_fill_desc(cur_trb, desc_dma, 0, 0,
				   DWC_DSCCTL_TRBCTL_LINK, 0, 1);
	}

	*desc_dma_ret = desc_dma;
	return desc;
}

/**
 * This function enables an EP
 */
static int ep_enable(struct usb_ep *usb_ep,
		     const struct usb_endpoint_descriptor *ep_desc)
{
#ifdef DWC_MULTI_GADGET
	unsigned devnum = usb_ep->devnum;
#else
	unsigned devnum = 0;
#endif
	struct gadget_wrapper *wrapper;
	dwc_usb3_pcd_ep_t *pcd_ep;
	dwc_usb3_pcd_t *pcd;
	dma_addr_t desc_dma;
	char *desc;
	int desc_size, num, dir, type, num_trbs, maxpacket;
	int retval = 0;
	unsigned long flags;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0)
	int mult = 0, maxburst = 0, maxstreams = 0;
#endif

#if defined(DEBUG) || defined(ISOC_DEBUG)
	printk(KERN_DEBUG USB3_DWC "%s(%p,%p)\n", __func__, usb_ep, ep_desc);
	printk(KERN_DEBUG USB3_DWC "devnum=%u\n", devnum);
#endif
	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		printk(KERN_WARNING USB3_DWC
		       "%s, bad devnum %u or null wrapper!\n",
		       __func__, devnum);
		return -EINVAL;
	}

	wrapper = gadget_wrapper[devnum];

	if (!usb_ep || !ep_desc ||
	    ep_desc->bDescriptorType != USB_DT_ENDPOINT) {
		printk(KERN_WARNING USB3_DWC
		       "%s, bad ep or descriptor %p %p %d!\n",
		       __func__, usb_ep, ep_desc,
		       ep_desc ? ep_desc->bDescriptorType : 0);
		return -EINVAL;
	}

#if defined(DEBUG) || defined(ISOC_DEBUG)
	printk(USB3_DWC "usb_ep->name = %s\n", usb_ep->name);
#endif
	if (usb_ep == &wrapper->ep0.usb_ep) {
		printk(KERN_WARNING USB3_DWC "%s called for EP0!\n", __func__);
		return -EINVAL;
	}

	if (!ep_desc->wMaxPacketSize) {
		printk(KERN_WARNING USB3_DWC "%s, zero %s wMaxPacketSize!\n",
		       __func__, usb_ep->name);
		return -ERANGE;
	}

	if (!wrapper->driver || wrapper->gadget.speed == USB_SPEED_UNKNOWN) {
		printk(KERN_WARNING USB3_DWC "%s, bogus device state!\n",
		       __func__);
		return -ESHUTDOWN;
	}

#if defined(DEBUG) || defined(ISOC_DEBUG)
	printk(KERN_DEBUG USB3_DWC "usb_ep->maxpacket = %d\n",
	       le16_to_cpu(ep_desc->wMaxPacketSize));
#endif
	pcd = wrapper->pcd;

	spin_lock_irqsave(&pcd->lock, flags);

	pcd_ep = dwc_usb3_get_pcd_ep(usb_ep);

	if (pcd_ep->dwc_ep.phys >= DWC_MAX_PHYS_EP) {
		dwc_warn(pcd->usb3_dev, "%s, bad %s phys EP # %d!\n",
			 __func__, usb_ep->name, pcd_ep->dwc_ep.phys);
		spin_unlock_irqrestore(&pcd->lock, flags);
		return -ERANGE;
	}

	/* Free any existing TRB allocation for this EP */
	if (pcd_ep->dwc_ep.dma_desc) {
		desc = pcd_ep->dwc_ep.dma_desc;
		desc_dma = pcd_ep->dwc_ep.dma_desc_dma;
		desc_size = pcd_ep->dwc_ep.dma_desc_size;
		pcd_ep->dwc_ep.dma_desc = NULL;
		pcd_ep->dwc_ep.dma_desc_dma = 0;

		spin_unlock_irqrestore(&pcd->lock, flags);
		dma_free_coherent(NULL, desc_size, desc, desc_dma);
	} else {
		spin_unlock_irqrestore(&pcd->lock, flags);
	}

	num = UE_GET_ADDR(ep_desc->bEndpointAddress);
	dir = UE_GET_DIR(ep_desc->bEndpointAddress);
	type = UE_GET_XFERTYPE(ep_desc->bmAttributes);

	/* Allocate the number of TRBs based on EP type */
	switch (type) {
	case UE_INTERRUPT:
		num_trbs = DWC_NUM_INTR_TRBS;
		desc_size = num_trbs * 16;
		break;
	case UE_ISOCHRONOUS:
		num_trbs = DWC_NUM_ISOC_TRBS + 1; /* +1 for link TRB */
#ifdef DWC_TEST_ISOC_CHAIN
		desc_size = (DWC_NUM_ISOC_TRBS * 3 + 1) * 16;
#else
		desc_size = num_trbs * 16;
#endif
		break;
	default:
		num_trbs = DWC_NUM_BULK_TRBS;
		desc_size = num_trbs * 16;
		break;
	}

	dwc_debug(pcd->usb3_dev, "ep%d-%s=%p phys=%d pcd_ep=%p\n",
		  num, dir == UE_DIR_IN ? "IN" : "OUT", usb_ep,
		  pcd_ep->dwc_ep.phys, pcd_ep);

	/* Set the TRB allocation for this EP */
	desc = trb_alloc(pcd, type, ep_desc->bInterval-1, num_trbs,
			 desc_size, &desc_dma);
	if (!desc)
		return -ENOMEM;

	maxpacket = le16_to_cpu(ep_desc->wMaxPacketSize);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0)
	if (wrapper->gadget.speed == USB_SPEED_SUPER) {
		if (usb_ep->comp_desc) {
			if (type == UE_ISOCHRONOUS)
				mult = usb_ep->comp_desc->bmAttributes;
			maxburst = usb_ep->comp_desc->bMaxBurst;
			if (type == UE_BULK)
				maxstreams = usb_ep->comp_desc->bmAttributes;
		}
	} else {
		if (type == UE_ISOCHRONOUS)
			mult = (maxpacket >> 11) & 3;
	}

	if (mult > 2 || maxburst > 15 || maxstreams > 16)
		return -EINVAL;

	usb_ep->mult = mult;
	usb_ep->maxburst = maxburst;
	usb_ep->max_streams = maxstreams;
#endif
	spin_lock_irqsave(&pcd->lock, flags);
	dwc_wait_if_hibernating(pcd, flags);
	atomic_inc(&pcd->usb3_dev->hiber_cnt);

	/* Init the pcd_ep structure */
	pcd_ep->dwc_ep.dma_desc = desc;
	pcd_ep->dwc_ep.dma_desc_dma = desc_dma;
	pcd_ep->dwc_ep.dma_desc_size = desc_size;
	pcd_ep->dwc_ep.num_desc =
		type == UE_ISOCHRONOUS ? num_trbs - 1 : num_trbs;
	pcd_ep->dwc_ep.desc_avail = pcd_ep->dwc_ep.num_desc;
	pcd_ep->dwc_ep.desc_idx = 0;
	pcd_ep->dwc_ep.hiber_desc_idx = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0)
	pcd_ep->dwc_ep.num_streams = usb_ep->max_streams;
#else
	pcd_ep->dwc_ep.num_streams = usb_ep->numstreams;
#endif
	pcd_ep->dwc_ep.mult = usb_ep->mult;
	pcd_ep->dwc_ep.maxburst = usb_ep->maxburst;

	retval = dwc_usb3_pcd_ep_enable(pcd, pcd_ep, (const void *)ep_desc);
	if (retval) {
		pcd_ep->dwc_ep.dma_desc = NULL;
		pcd_ep->dwc_ep.dma_desc_dma = 0;
	} else {
		usb_ep->maxpacket = maxpacket;
	}

	atomic_dec(&pcd->usb3_dev->hiber_cnt);
	spin_unlock_irqrestore(&pcd->lock, flags);

	if (retval) {
		dma_free_coherent(NULL, desc_size, desc, desc_dma);
		return -ENOMEM;
	}

	return retval;
}

/**
 * This function disables an EP
 */
static int ep_disable(struct usb_ep *usb_ep)
{
#ifdef DWC_MULTI_GADGET
	unsigned devnum = usb_ep->devnum;
#else
	unsigned devnum = 0;
#endif
	int retval = 0;
	dwc_usb3_pcd_ep_t *pcd_ep;
	dwc_usb3_pcd_t *pcd;
	dma_addr_t desc_dma;
	char *desc;
	int desc_size;
	unsigned long flags;

#if defined(DEBUG) || defined(ISOC_DEBUG)
	printk(KERN_DEBUG USB3_DWC "%s()\n", __func__);
	printk(KERN_DEBUG USB3_DWC "devnum=%u\n", devnum);
#endif
	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		printk(KERN_WARNING USB3_DWC
		       "%s, bad devnum %u or null wrapper!\n",
		       __func__, devnum);
		return -EINVAL;
	}

	if (!usb_ep) {
		printk(KERN_WARNING USB3_DWC "EP not enabled!\n");
		return -EINVAL;
	}

	pcd = gadget_wrapper[devnum]->pcd;

	spin_lock_irqsave(&pcd->lock, flags);
	dwc_wait_if_hibernating(pcd, flags);
	atomic_inc(&pcd->usb3_dev->hiber_cnt);

	pcd_ep = dwc_usb3_get_pcd_ep(usb_ep);
	retval = dwc_usb3_pcd_ep_disable(pcd, pcd_ep);

	desc = pcd_ep->dwc_ep.dma_desc;
	desc_dma = pcd_ep->dwc_ep.dma_desc_dma;
	desc_size = pcd_ep->dwc_ep.dma_desc_size;

	if (desc) {
		pcd_ep->dwc_ep.dma_desc = NULL;
		pcd_ep->dwc_ep.dma_desc_dma = 0;
	}

	atomic_dec(&pcd->usb3_dev->hiber_cnt);
	spin_unlock_irqrestore(&pcd->lock, flags);

	/* Free any existing TRB allocation for this EP */
	if (desc)
		dma_free_coherent(NULL, desc_size, desc, desc_dma);

	if (retval)
		return -EINVAL;
	return 0;
}

/**
 * This function allocates a request object to use with the specified USB EP.
 *
 * @param usb_ep    The USB EP to be used with with the request.
 * @param gfp_flags The GFP_* flags to use.
 */
static struct usb_request *alloc_request(struct usb_ep *usb_ep, gfp_t gfp_flags)
{
	dwc_usb3_pcd_req_t *pcd_req;

#if defined(DEBUG) || defined(ISOC_DEBUG)
	printk(KERN_DEBUG USB3_DWC "%s(%p,%d)\n", __func__, usb_ep, gfp_flags);
#endif
	if (!usb_ep) {
		printk(USB3_DWC "%s() %s\n", __func__, "Invalid EP!\n");
		return NULL;
	}

	pcd_req = kmalloc(sizeof(dwc_usb3_pcd_req_t), gfp_flags);
	if (!pcd_req) {
		printk(USB3_DWC "%s() pcd request allocation failed!\n",
		       __func__);
		return NULL;
	}

#if defined(DEBUG) || defined(ISOC_DEBUG)
	//printk(USB3_DWC "%s() allocated %p\n", __func__, pcd_req);
#endif
	memset(pcd_req, 0, sizeof(dwc_usb3_pcd_req_t));
	pcd_req->usb_req.dma = DWC_DMA_ADDR_INVALID;

#ifdef DWC_TEST_ISOC_CHAIN
	pcd_req->usb_req.dma2[0] = DWC_DMA_ADDR_INVALID;
	pcd_req->usb_req.dma2[1] = DWC_DMA_ADDR_INVALID;
#endif
	return &pcd_req->usb_req;
}

/**
 * This function frees a request object.
 *
 * @param usb_ep  The USB EP associated with the request.
 * @param usb_req The request being freed.
 */
static void free_request(struct usb_ep *usb_ep, struct usb_request *usb_req)
{
	dwc_usb3_pcd_req_t *pcd_req;

#if defined(DEBUG) || defined(ISOC_DEBUG)
	printk(KERN_DEBUG USB3_DWC "%s(%p,%p)\n", __func__, usb_ep, usb_req);
#endif
	if (!usb_ep || !usb_req) {
		printk(KERN_WARNING USB3_DWC "%s() %s\n", __func__,
		       "Invalid ep or req argument!\n");
		return;
	}

	pcd_req = dwc_usb3_get_pcd_req(usb_req);

#if defined(DEBUG) || defined(ISOC_DEBUG)
	//printk(USB3_DWC "%s() freed %p\n", __func__, pcd_req);
#endif
	kfree(pcd_req);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
/**
 * This function allocates an I/O buffer to be used for a transfer
 * to/from the specified USB EP.
 *
 * @param usb_ep    The USB EP to be used with with the request.
 * @param bytes     The desired number of bytes for the buffer.
 * @param dma       Pointer to the buffer's DMA address; must be valid.
 * @param gfp_flags The GFP_* flags to use.
 * @return          Address of a new buffer or null if buffer could not be
 *                  allocated.
 */
static void *alloc_buffer(struct usb_ep *usb_ep, unsigned bytes,
			  dma_addr_t *dma, gfp_t gfp_flags)
{
	gfp_t gfp_flags = GFP_KERNEL | GFP_DMA;
	void *buf;

#if defined(DEBUG) || defined(ISOC_DEBUG)
	printk(KERN_DEBUG USB3_DWC "%s(%p,%d,%p,%0x)\n", __func__,
	       usb_ep, bytes, dma, gfp_flags);
#endif
	/* Check dword alignment */
	if ((bytes & 0x3) != 0) {
		printk(KERN_WARNING USB3_DWC
		      "%s() Buffer size is not a multiple of DWORD size (%d)\n",
		      __func__, bytes);
	}

	buf = dma_alloc_coherent(NULL, bytes, dma, gfp_flags);
	if (buf) {
		/* Check dword alignment */
		if (((unsigned long)buf & 0x3) != 0) {
			printk(KERN_WARNING USB3_DWC
			       "%s() Buffer is not DWORD aligned (%p)\n",
			       __func__, buf);
		}
	}

	return buf;
}

/**
 * This function frees an I/O buffer that was allocated by alloc_buffer.
 *
 * @param usb_ep The USB EP associated with the buffer.
 * @param buf    Address of the buffer.
 * @param dma    The buffer's DMA address.
 * @param bytes  The number of bytes of the buffer.
 */
static void free_buffer(struct usb_ep *usb_ep, void *buf,
			dma_addr_t dma, unsigned bytes)
{
#if defined(DEBUG) || defined(ISOC_DEBUG)
	printk(KERN_DEBUG USB3_DWC "%s(%p,%p,%0x,%d)\n", __func__,
	       usb_ep, buf, dma, bytes);
#endif
	dma_free_coherent(NULL, bytes, buf, dma);
}
#endif

/**
 * This function queues a request to a USB EP
 */
static int ep_queue(struct usb_ep *usb_ep, struct usb_request *usb_req,
		    gfp_t gfp_flags)
{
#ifdef DWC_MULTI_GADGET
	unsigned devnum = usb_ep->devnum;
#else
	unsigned devnum = 0;
#endif
	int is_atomic = 0;
	struct gadget_wrapper *wrapper;
	int retval = 0;
	int req_flags = 0;
	int q_empty = 0;
	dwc_usb3_pcd_ep_t *pcd_ep;
	dwc_usb3_pcd_req_t *pcd_req;
#ifdef CONFIG_HITECH
	struct pci_dev *dev;
#endif
	int numbufs;
	void *bufs[3];
	dma_addr_t bufdmas[3];
	uint32_t buflens[3];
	dwc_usb3_pcd_t *pcd;
	unsigned long flags;

#ifdef DEBUG
	printk(KERN_DEBUG USB3_DWC "%s(%p,%p,%d)\n", __func__,
	       usb_ep, usb_req, gfp_flags);
	printk(KERN_DEBUG USB3_DWC "devnum=%u\n", devnum);
#endif
	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		printk(KERN_WARNING USB3_DWC
		       "%s, bad devnum %u or null wrapper\n", __func__, devnum);
		return -EINVAL;
	}

	wrapper = gadget_wrapper[devnum];

#ifdef DEBUG
	printk(KERN_DEBUG USB3_DWC "pcd=%p pcd->devnum=%u\n",
	       wrapper->pcd, wrapper->pcd->devnum);
#endif
	if (!usb_req || !usb_req->complete || !usb_req->buf) {
		printk(KERN_WARNING USB3_DWC "%s, bad params\n", __func__);
		return -EINVAL;
	}

	if (!usb_ep) {
		printk(KERN_WARNING USB3_DWC "%s, bad ep\n", __func__);
		return -EINVAL;
	}

	if (!wrapper->driver || wrapper->gadget.speed == USB_SPEED_UNKNOWN) {
		printk(KERN_DEBUG USB3_DWC "gadget.speed=%d\n",
		       wrapper->gadget.speed);
		printk(KERN_WARNING USB3_DWC "%s, bogus device state\n",
		       __func__);
		return -ESHUTDOWN;
	}

	if (!wrapper->pcd) {
		printk(KERN_WARNING USB3_DWC
		       "%s, gadget_wrapper->pcd is NULL!\n", __func__);
		return -EINVAL;
	}

	pcd_ep = dwc_usb3_get_pcd_ep(usb_ep);
	pcd_req = dwc_usb3_get_pcd_req(usb_req);
	pcd = dwc_usb3_pcd_ep_to_pcd(pcd_ep);

#ifdef DEBUG
	printk(KERN_DEBUG USB3_DWC "%s 0x%p queue req 0x%p, len %d buf 0x%p\n",
	       usb_ep->name, pcd_ep, usb_req, usb_req->length, usb_req->buf);
#endif
	usb_req->status = -EINPROGRESS;
	usb_req->actual = 0;

	if (gfp_flags == GFP_ATOMIC)
		is_atomic = 1;

	if (usb_req->zero)
		req_flags |= DWC_PCD_REQ_ZERO;

	if (usb_req->length != 0 && usb_req->dma == DWC_DMA_ADDR_INVALID) {
		dwc_debug(pcd->usb3_dev, "DMA map req %p\n", usb_req);
#ifdef CONFIG_HITECH
		dev = pcd->usb3_dev->pcidev;
		dwc_debug(pcd->usb3_dev, "dev=%p\n", dev);
		map_buffers(dev, usb_req, pcd_ep, &req_flags);
#endif
#ifdef CONFIG_IPMATE
		/* @todo Implement for IPMate */
#endif
	}

#ifdef DWC_UASP_GADGET_STREAMS
	dwc_debug(pcd->usb3_dev,
		  "GADGET streams enabled, queueing request with stream %d\n",
		  usb_req->stream_id);
#else
	dwc_debug(pcd->usb3_dev,
		  "GADGET streams disabled, queueing request with stream %d\n",
		  usb_req->stream_id);
#endif
	bufs[0] = usb_req->buf;
	bufdmas[0] = usb_req->dma;
	buflens[0] = usb_req->length;
	numbufs = 1;

#ifdef DWC_TEST_ISOC_CHAIN
	if (dwc_usb3_pcd_ep_type(pcd_ep) == UE_ISOCHRONOUS) {
		buflens[0] = usb_req->length - 58;

		bufs[1] = usb_req->buf2[0];
		bufdmas[1] = usb_req->dma2[0];
		buflens[1] = 29;

		bufs[2] = usb_req->buf2[1];
		bufdmas[2] = usb_req->dma2[1];
		buflens[2] = 29;

		numbufs = 3;
	}
#endif
	if (dwc_usb3_pcd_ep_num(pcd_ep) != 0 && !pcd_ep->dwc_ep.usb_ep_desc) {
		dwc_debug(pcd->usb3_dev, "%s, bad ep!\n", __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&pcd->lock, flags);
	dwc_wait_if_hibernating(pcd, flags);
	atomic_inc(&pcd->usb3_dev->hiber_cnt);

	dwc_debug(pcd->usb3_dev, "%s: EP%d-%s %p stream %d req %p\n",
		  __func__, dwc_usb3_pcd_ep_num(pcd_ep),
		  dwc_usb3_pcd_ep_is_in(pcd_ep) ? "IN" : "OUT",
		  pcd_ep, usb_req->stream_id, pcd_req);

	if (list_empty(&pcd_ep->dwc_ep.queue))
		q_empty = 1;

	dwc_debug(pcd->usb3_dev, "%s(%p,%p,%p,%p,%lx)\n", __func__, pcd,
		  usb_ep, usb_req, bufs[0], (unsigned long)bufdmas[0]);

	INIT_LIST_HEAD(&pcd_req->entry);
	dwc_usb3_pcd_fill_req(pcd, pcd_ep, pcd_req, numbufs, bufs,
			      bufdmas, buflens,
#ifdef DWC_UASP_GADGET_STREAMS
			      usb_req->stream_id,
#else
			      0,
#endif
			      req_flags);
	retval = dwc_usb3_pcd_ep_queue(pcd, pcd_ep, pcd_req, req_flags,
				       q_empty);
	if (!retval) {
		list_add_tail(&pcd_req->entry, &pcd_ep->dwc_ep.queue);
		++pcd->request_pending;
	}

	atomic_dec(&pcd->usb3_dev->hiber_cnt);
	spin_unlock_irqrestore(&pcd->lock, flags);

	if (retval < 0)
		return -EINVAL;
	return 0;
}

/**
 * This function dequeues a request from a USB EP
 */
static int ep_dequeue(struct usb_ep *usb_ep, struct usb_request *usb_req)
{
#ifdef DWC_MULTI_GADGET
	unsigned devnum = usb_ep->devnum;
#else
	unsigned devnum = 0;
#endif
	struct gadget_wrapper *wrapper;
	dwc_usb3_pcd_req_t *pcd_req = NULL;
	dwc_usb3_pcd_ep_t *pcd_ep;
	dwc_usb3_pcd_t *pcd;
	struct list_head *list_item;
	unsigned long flags;

#ifdef DEBUG
	printk(KERN_DEBUG USB3_DWC "%s(%p,%p)\n", __func__, usb_ep, usb_req);
	printk(KERN_DEBUG USB3_DWC "devnum=%u\n", devnum);
#endif
	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		printk(KERN_WARNING USB3_DWC
		       "%s, bad devnum %u or null wrapper!\n",
		       __func__, devnum);
		return -EINVAL;
	}

	wrapper = gadget_wrapper[devnum];

	if (!usb_ep || !usb_req) {
		printk(KERN_WARNING USB3_DWC "%s, bad argument!\n", __func__);
		return -EINVAL;
	}

	if (!wrapper->driver || wrapper->gadget.speed == USB_SPEED_UNKNOWN) {
		printk(KERN_WARNING USB3_DWC "%s, bogus device state!\n",
		       __func__);
		return -ESHUTDOWN;
	}

	if (!wrapper->pcd) {
		printk(KERN_WARNING USB3_DWC
		       "%s, gadget_wrapper->pcd is NULL!\n", __func__);
		return -EINVAL;
	}

	pcd = wrapper->pcd;

	spin_lock_irqsave(&pcd->lock, flags);
	dwc_wait_if_hibernating(pcd, flags);
	atomic_inc(&pcd->usb3_dev->hiber_cnt);

	pcd_ep = dwc_usb3_get_pcd_ep(usb_ep);

	if (dwc_usb3_pcd_ep_num(pcd_ep) != 0 && !pcd_ep->dwc_ep.usb_ep_desc) {
		//dwc_warn(pcd->usb3_dev, "%s, bad pcd_ep!\n", __func__);
		atomic_dec(&pcd->usb3_dev->hiber_cnt);
		spin_unlock_irqrestore(&pcd->lock, flags);
		return -EINVAL;
	}

	/* make sure it's actually queued on this EP */
	list_for_each (list_item, &pcd_ep->dwc_ep.queue) {
		pcd_req = list_entry(list_item, dwc_usb3_pcd_req_t, entry);
		if (&pcd_req->usb_req == usb_req)
			break;
	}

	if (!pcd_req) {
		dwc_warn(pcd->usb3_dev, "%s, no request in queue!\n", __func__);
		atomic_dec(&pcd->usb3_dev->hiber_cnt);
		spin_unlock_irqrestore(&pcd->lock, flags);
		return 0;
	}

	dwc_usb3_pcd_ep_dequeue(pcd, pcd_ep, pcd_req,
#ifdef DWC_UASP_GADGET_STREAMS
				usb_req->stream_id
#else
				0
#endif
			       );

	atomic_dec(&pcd->usb3_dev->hiber_cnt);
	spin_unlock_irqrestore(&pcd->lock, flags);
	return 0;
}

/**
 * This function sets/clears halt on a USB EP
 */
static int ep_set_halt(struct usb_ep *usb_ep, int value)
{
#ifdef DWC_MULTI_GADGET
	unsigned devnum = usb_ep->devnum;
#else
	unsigned devnum = 0;
#endif
	dwc_usb3_pcd_ep_t *pcd_ep;
	dwc_usb3_pcd_t *pcd;
	unsigned long flags;

#if defined(DEBUG) || defined(ISOC_DEBUG)
	printk(KERN_DEBUG USB3_DWC "HALT %s %d\n", usb_ep->name, value);
	printk(KERN_DEBUG USB3_DWC "devnum=%u\n", devnum);
#endif
	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		printk(KERN_WARNING USB3_DWC
		       "%s, bad devnum %u or null wrapper!\n",
		       __func__, devnum);
		return -EINVAL;
	}

	if (!usb_ep) {
		printk(KERN_WARNING USB3_DWC "%s, bad usb_ep!\n", __func__);
		return -EINVAL;
	}

	pcd = gadget_wrapper[devnum]->pcd;

	spin_lock_irqsave(&pcd->lock, flags);
	dwc_wait_if_hibernating(pcd, flags);
	atomic_inc(&pcd->usb3_dev->hiber_cnt);

	pcd_ep = dwc_usb3_get_pcd_ep(usb_ep);
	dwc_debug(pcd->usb3_dev, "pcd_ep=%p\n", pcd_ep);
	dwc_debug(pcd->usb3_dev, "pcd->ep0=%p\n", pcd->ep0);
	dwc_debug(pcd->usb3_dev, "epnum=%d is_in=%d\n",
		  dwc_usb3_pcd_ep_num(pcd_ep), dwc_usb3_pcd_ep_is_in(pcd_ep));

	if ((!pcd_ep->dwc_ep.usb_ep_desc && dwc_usb3_pcd_ep_num(pcd_ep) != 0) ||
	    (pcd_ep->dwc_ep.usb_ep_desc && dwc_usb3_pcd_ep_type(pcd_ep)
							== UE_ISOCHRONOUS)) {
		dwc_warn(pcd->usb3_dev, "%s, bad pcd_ep!\n", __func__);
		atomic_dec(&pcd->usb3_dev->hiber_cnt);
		spin_unlock_irqrestore(&pcd->lock, flags);
		return -EINVAL;
	}

	if (!list_empty(&pcd_ep->dwc_ep.queue)) {
		dwc_warn(pcd->usb3_dev, "%s, Xfer in process!\n", __func__);
		atomic_dec(&pcd->usb3_dev->hiber_cnt);
		spin_unlock_irqrestore(&pcd->lock, flags);
		return -EAGAIN;
	}

	dwc_usb3_pcd_ep_set_halt(pcd, pcd_ep, value);

	atomic_dec(&pcd->usb3_dev->hiber_cnt);
	spin_unlock_irqrestore(&pcd->lock, flags);
	return 0;
}

static struct usb_ep_ops pcd_ep_ops = {
	.enable		= ep_enable,
	.disable	= ep_disable,

	.alloc_request	= alloc_request,
	.free_request	= free_request,

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
	.alloc_buffer	= alloc_buffer,
	.free_buffer	= free_buffer,
#endif
	.queue		= ep_queue,
	.dequeue	= ep_dequeue,

	.set_halt	= ep_set_halt,
	.fifo_status	= NULL,
	.fifo_flush	= NULL,
};

/* gadget ops */

#ifdef CONFIG_USB_OTG_DWC


#if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0)
static int pcd_stop_peripheral(struct usb_gadget *gadget)
#else
static int pcd_stop_peripheral(struct usb_gadget *gadget,
			       struct usb_gadget_driver *driver)
#endif
{
	struct gadget_wrapper *d;
	dwc_usb3_pcd_t *pcd;
	uint32_t temp;

	d = container_of(gadget, struct gadget_wrapper, gadget);
	pcd = d->pcd;

	spin_lock(&pcd->lock);
	dwc_usb3_pcd_stop(pcd);

	/* Clear Run/Stop bit */
	temp = dwc_rd32(pcd->usb3_dev, &pcd->dev_global_regs->dctl);
	temp &= ~DWC_DCTL_RUN_STOP_BIT;
	dwc_wr32(pcd->usb3_dev, &pcd->dev_global_regs->dctl, temp);

	/* Disable device interrupts */
	dwc_wr32(pcd->usb3_dev, &pcd->dev_global_regs->devten, 0);
	spin_unlock(&pcd->lock);

	/* Wait for core stopped */
	do {
		msleep(1);
		temp = dwc_rd32(pcd->usb3_dev, &pcd->dev_global_regs->dsts);
	} while (!(temp & DWC_DSTS_DEV_CTRL_HLT_BIT));

	msleep(10);
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0)
static int pcd_start_peripheral(struct usb_gadget *gadget)
#else
static int pcd_start_peripheral(struct usb_gadget *gadget,
				struct usb_gadget_driver *driver)
#endif
{
	struct gadget_wrapper *d;
	dwc_usb3_pcd_t *pcd;
	uint32_t temp;

	d = container_of(gadget, struct gadget_wrapper, gadget);
	pcd = d->pcd;

	spin_lock(&pcd->lock);
	dwc_usb3_core_device_init(pcd->usb3_dev, 0, 0);

	/* Enable Device mode interrupts */
	dwc_usb3_enable_device_interrupts(pcd->usb3_dev);

	/* Set Run/Stop bit, and Keep-Connect bit if hibernation enabled */
	temp = dwc_rd32(pcd->usb3_dev, &pcd->dev_global_regs->dctl);
	temp |= DWC_DCTL_RUN_STOP_BIT;
	if (pcd->usb3_dev->core_params->hibernate &&
	    (pcd->usb3_dev->hwparams1 & DWC_HWP1_EN_PWROPT_BITS) ==
	    (DWC_EN_PWROPT_HIBERNATION << DWC_HWP1_EN_PWROPT_SHIFT))
		temp |= DWC_DCTL_KEEP_CONNECT_BIT;
	dwc_wr32(pcd->usb3_dev, &pcd->dev_global_regs->dctl, temp);

	pcd->wants_host = 0;
	spin_unlock(&pcd->lock);

	msleep(10);
	return 0;
}

static int pcd_send_hrr(struct usb_gadget *gadget, int is_init)
{
	struct gadget_wrapper *d;
	dwc_usb3_pcd_t *pcd;
	u32 param;

	d = container_of(gadget, struct gadget_wrapper, gadget);
	pcd = d->pcd;
	param = is_init ? DWC_DGCMDPAR_HOST_ROLE_REQ_INITIATE :
		DWC_DGCMDPAR_HOST_ROLE_REQ_CONFIRM;

	dwc_usb3_xmit_host_role_request(pcd, param);
	return 0;
}

#endif

/**
 * This function returns the current USB frame number.
 *
 * @param gadget The gadget context.
 */
static int dwc_get_frame(struct usb_gadget *gadget)
{
	struct gadget_wrapper	*d;
	dwc_usb3_pcd_t		*pcd;
	dwc_usb3_device_t	*dev;
	unsigned long		flags;
	int			ret;

#if defined(DEBUG) || defined(ISOC_DEBUG)
	printk(KERN_DEBUG USB3_DWC "%s()\n", __func__);
#endif
	if (!gadget)
		return -ENODEV;

	d = container_of(gadget, struct gadget_wrapper, gadget);
	pcd = d->pcd;
	dev = pcd->usb3_dev;

	spin_lock_irqsave(&pcd->lock, flags);
	dwc_wait_if_hibernating(pcd, flags);
	atomic_inc(&pcd->usb3_dev->hiber_cnt);

	ret = dwc_usb3_get_frame_number(pcd);

	atomic_dec(&pcd->usb3_dev->hiber_cnt);
	spin_unlock_irqrestore(&pcd->lock, flags);
	return ret;
}

/**
 * This function sends a remote wakeup to the host.
 *
 * @param gadget The gadget context.
 */
int dwc_usb3_wakeup(struct usb_gadget *gadget)
{
	struct gadget_wrapper	*d;
	dwc_usb3_pcd_t		*pcd;
	dwc_usb3_device_t	*dev;
	unsigned long		timeout, flags;
	int			state;

#if defined(DEBUG) || defined(ISOC_DEBUG)
	printk(KERN_DEBUG USB3_DWC "%s()\n", __func__);
#endif
	if (!gadget)
		return -ENODEV;

	d = container_of(gadget, struct gadget_wrapper, gadget);
	pcd = d->pcd;
	dev = pcd->usb3_dev;

	if (!pcd->remote_wakeup_enable) {
		dwc_info0(dev, "hardware not enabled for wakeup\n");
		return -ENOTSUPP;
	}

	spin_lock_irqsave(&pcd->lock, flags);
	dwc_wait_if_hibernating(pcd, flags);
	atomic_inc(&pcd->usb3_dev->hiber_cnt);

	state = dwc_usb3_get_link_state(pcd);
	dwc_usb3_set_link_state(pcd, DWC_LINK_STATE_REQ_REMOTE_WAKEUP);
	spin_unlock_irqrestore(&pcd->lock, flags);

	dwc_info1(dev, "link state before: %d\n", state);
#if 0
	/* Clear DCTL bits after 2 ms */
	msleep(2);

	spin_lock_irqsave(&pcd->lock, flags);
	dwc_usb3_set_link_state(pcd, 0);
	spin_unlock_irqrestore(&pcd->lock, flags);
#endif
	/* Wait 500 ms for link state to go to U0 */
	timeout = jiffies + msecs_to_jiffies(500);

	while (!time_after(jiffies, timeout)) {
		spin_lock_irqsave(&pcd->lock, flags);
		state = dwc_usb3_get_link_state(pcd);
		spin_unlock_irqrestore(&pcd->lock, flags);
		if (state == DWC_LINK_STATE_ON)
			break;
		else
			msleep(1);
	}

	dwc_info1(dev, "link state after: %d\n", state);

	if (state != DWC_LINK_STATE_ON) {
		atomic_dec(&pcd->usb3_dev->hiber_cnt);
		return -ETIMEDOUT;
	}

	/* Send function remote wake notification */
	spin_lock_irqsave(&pcd->lock, flags);
	dwc_usb3_remote_wake(pcd, 0);
	pcd->wkup_rdy = 0;
	atomic_dec(&pcd->usb3_dev->hiber_cnt);
	spin_unlock_irqrestore(&pcd->lock, flags);
	dwc_info0(dev, "remote wake sent\n");

	return 0;
}

extern int dwc_usb3_driver_force_init(void);
static int dwc_usb3_reset(struct usb_gadget *gadget, int is_on)
{
	struct gadget_wrapper	*d;
	dwc_usb3_pcd_t		*pcd;
	dwc_usb3_device_t	*dev;
   int state, retval = 0;

   unsigned int uDEVTEN;   
   uint32_t addr_ofs = 0xc000;
   struct resource *resource;

   d = container_of(gadget, struct gadget_wrapper, gadget);
   pcd = d->pcd;
   dev = pcd->usb3_dev;

   printk(KERN_INFO "USB3 Port Force Init %d\n", is_on);

   if (is_on) {
      dwc_usb3_driver_force_init();
   } else {
      dwc_usb3_set_link_state(pcd, 0);
   }
}


static const struct usb_gadget_ops pcd_ops = {
	.get_frame	= dwc_get_frame,
	.wakeup		= dwc_usb3_wakeup,
   .pullup     = dwc_usb3_reset,
	// current versions must always be self-powered
#ifdef CONFIG_USB_OTG_DWC
# if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0)
	.start		= pcd_start_peripheral,
	.stop		= pcd_stop_peripheral,
# else
	.udc_start	= pcd_start_peripheral,
	.udc_stop	= pcd_stop_peripheral,
# endif
	.send_hrr	= pcd_send_hrr,
#endif
};


/*=======================================================================*/

/**
 * Initialize the Linux gadget specific parts of the default Control EP (EP0).
 */
static void gadget_init_ep0(dwc_usb3_pcd_t *pcd, struct gadget_wrapper *d)
{
	dwc_usb3_pcd_ep_t *pcd_ep;
	struct usb_ep *ep0;

	dwc_debug(pcd->usb3_dev, "%s()\n", __func__);

	d->gadget.ep0 = &d->ep0.usb_ep;
	INIT_LIST_HEAD(&d->gadget.ep0->ep_list);

	pcd_ep = &d->ep0;
	ep0 = &pcd_ep->usb_ep;
	dwc_debug(pcd->usb3_dev, "ep0=%p\n", ep0);

	INIT_LIST_HEAD(&pcd_ep->dwc_ep.queue);

	/* Init the usb_ep structure */
	ep0->name = d->ep_names[0];
	ep0->ops = (struct usb_ep_ops *)&pcd_ep_ops;
	ep0->maxpacket = DWC_MAX_EP0_SIZE;
	dwc_debug(pcd->usb3_dev, "EP0 name=%s\n", ep0->name);
	dwc_debug(pcd->usb3_dev, "ep0 eplist pre: %p(%p,%p) = %p(%p,%p)\n",
		  &d->gadget.ep0->ep_list, d->gadget.ep0->ep_list.prev,
		  d->gadget.ep0->ep_list.next, &ep0->ep_list,
		  ep0->ep_list.prev, ep0->ep_list.next);
	list_add_tail(&ep0->ep_list, &d->gadget.ep0->ep_list);
	dwc_debug(pcd->usb3_dev, "ep0 eplist post: %p(%p,%p) = %p(%p,%p)\n",
		  &d->gadget.ep0->ep_list, d->gadget.ep0->ep_list.prev,
		  d->gadget.ep0->ep_list.next, &ep0->ep_list,
		  ep0->ep_list.prev, ep0->ep_list.next);
}

/**
 * Initialize the Linux gadget specific parts of the non-EP0 EPs.
 */
static int gadget_init_eps(dwc_usb3_pcd_t *pcd, struct gadget_wrapper *d)
{
	dwc_usb3_pcd_ep_t *pcd_ep;
	struct usb_ep *ep;
	const char *name, *tmp, *ttmp;
	int i, num, ep_in, ep_out;

	dwc_debug(pcd->usb3_dev, "%s()\n", __func__);

	INIT_LIST_HEAD(&d->gadget.ep_list);

	for (i = 1, ep_in = 0, ep_out = 0; i < ARRAY_SIZE(d->ep_names); i++) {
		name = d->ep_names[i];
		if (!name || !name[0])
			break;

		/* Find '-' in e.g. "ep1in-bulk" */
		tmp = strrchr(name, '-');
		if (!tmp)
			/* If no '-' then find end of string */
			tmp = name + strlen(name);

		/* If not 0-len string then back up 1 char */
		if (tmp != name)
			tmp--;

		/* Get the EP number */
		num = 0;
		ttmp = tmp;

		if (*tmp == 't') {
			/* If "out", back up to the number before the 'o' */
			if (tmp >= name + 3)
				tmp -= 3;
		} else if (*tmp == 'n') {
			/* If "in", back up to the number before the 'i' */
			if (tmp >= name + 2)
				tmp -= 2;
		}

		if (isdigit(*tmp)) {
			/* If numeric, handle 1 or 2 digits */
			if (tmp > name && isdigit(*(tmp - 1)))
				num = simple_strtol(tmp - 1, NULL, 10);
			else
				num = simple_strtol(tmp, NULL, 10);
		}

		dwc_debug(pcd->usb3_dev, "num=%d\n", num);
		tmp = ttmp;
		if (num < 1 || num >= DWC_MAX_EPS)
			goto bad;

		/* If e.g. "ep1" or "ep1out" then OUT ep */
		if (isdigit(*tmp) || *tmp == 't') {
			ep_out++;
			pcd_ep = &d->out_ep[ep_out - 1];
			ep = &pcd_ep->usb_ep;
			dwc_debug(pcd->usb3_dev,
				  "ep%d-OUT=%p name=%s phys=%d pcd_ep=%p\n",
				  num, ep, name, ep_out, pcd_ep);

			pcd_ep->dwc_ep.num = num;
			pcd_ep->dwc_ep.dma_desc = NULL;
			INIT_LIST_HEAD(&pcd_ep->dwc_ep.queue);

			/* Init the usb_ep structure */
			ep->name = name;
			ep->ops = (struct usb_ep_ops *)&pcd_ep_ops;
			ep->maxpacket = DWC_MAX_PACKET_SIZE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0)
			ep->max_streams = 0;
#else
			ep->numstreams = 0;
#endif
			ep->mult = 0;
			ep->maxburst = 0;

			dwc_debug(pcd->usb3_dev,
				"ep%d-OUT eplist pre: %p(%p,%p) = %p(%p,%p)\n",
				num, &d->gadget.ep_list, d->gadget.ep_list.prev,
				d->gadget.ep_list.next, &ep->ep_list,
				ep->ep_list.prev, ep->ep_list.next);
			list_add_tail(&ep->ep_list, &d->gadget.ep_list);
			dwc_debug(pcd->usb3_dev,
				"ep%d-OUT eplist post: %p(%p,%p) = %p(%p,%p)\n",
				num, &d->gadget.ep_list, d->gadget.ep_list.prev,
				d->gadget.ep_list.next, &ep->ep_list,
				ep->ep_list.prev, ep->ep_list.next);
		}

		/* If e.g. "ep1" or "ep1in" then IN ep */
		if (isdigit(*tmp) || *tmp == 'n') {
			ep_in++;
			pcd_ep = &d->in_ep[ep_in - 1];
			ep = &pcd_ep->usb_ep;
			dwc_debug(pcd->usb3_dev,
				"ep%d-IN=%p name=%s phys=%d pcd_ep=%p\n",
				num, ep, name, ep_in, pcd_ep);

			pcd_ep->dwc_ep.num = num;
			pcd_ep->dwc_ep.dma_desc = NULL;
			INIT_LIST_HEAD(&pcd_ep->dwc_ep.queue);

			/* Init the usb_ep structure */
			ep->name = name;
			ep->ops = (struct usb_ep_ops *)&pcd_ep_ops;
			ep->maxpacket = DWC_MAX_PACKET_SIZE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0)
			ep->max_streams = 0;
#else
			ep->numstreams = 0;
#endif
			ep->mult = 0;
			ep->maxburst = 0;

			dwc_debug(pcd->usb3_dev,
				"ep%d-IN eplist pre: %p(%p,%p) = %p(%p,%p)\n",
				num, &d->gadget.ep_list, d->gadget.ep_list.prev,
				d->gadget.ep_list.next, &ep->ep_list,
				ep->ep_list.prev, ep->ep_list.next);
			list_add_tail(&ep->ep_list, &d->gadget.ep_list);
			dwc_debug(pcd->usb3_dev,
				"ep%d-IN eplist post: %p(%p,%p) = %p(%p,%p)\n",
				num, &d->gadget.ep_list, d->gadget.ep_list.prev,
				d->gadget.ep_list.next, &ep->ep_list,
				ep->ep_list.prev, ep->ep_list.next);
		}

		if (!isdigit(*tmp) && *tmp != 't' && *tmp != 'n') {
bad:
			dwc_debug(pcd->usb3_dev, "ep%d ????\n", num);
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * Free any descriptor allocations made for each non-EP0 EP.
 */
static void gadget_free_ep_allocations(dwc_usb3_pcd_t *pcd,
				       struct gadget_wrapper *d)
{
	dwc_usb3_pcd_ep_t *pcd_ep;
	dma_addr_t desc_dma;
	char *desc;
	int desc_size, i;

	for (i = DWC_MAX_EPS - 1; i > 0; i--) {
		pcd_ep = &d->in_ep[i - 1];

		if (pcd_ep->dwc_ep.dma_desc) {
			desc = pcd_ep->dwc_ep.dma_desc;
			desc_dma = pcd_ep->dwc_ep.dma_desc_dma;
			desc_size = pcd_ep->dwc_ep.dma_desc_size;
			pcd_ep->dwc_ep.dma_desc = NULL;
			pcd_ep->dwc_ep.dma_desc_dma = 0;
			dwc_debug(pcd->usb3_dev, "physpair%d-IN=%p\n",
				  i, pcd_ep);
			dma_free_coherent(NULL, desc_size, desc, desc_dma);
		}

		pcd_ep = &d->out_ep[i - 1];

		if (pcd_ep->dwc_ep.dma_desc) {
			desc = pcd_ep->dwc_ep.dma_desc;
			desc_dma = pcd_ep->dwc_ep.dma_desc_dma;
			desc_size = pcd_ep->dwc_ep.dma_desc_size;
			pcd_ep->dwc_ep.dma_desc = NULL;
			pcd_ep->dwc_ep.dma_desc_dma = 0;
			dwc_debug(pcd->usb3_dev, "physpair%d-OUT=%p\n",
				  i, pcd_ep);
			dma_free_coherent(NULL, desc_size, desc, desc_dma);
		}
	}
}

/**
 * This function releases the Gadget device.
 * Required by device_unregister().
 *
 * @param dev The device context.
 */
static void gadget_release(struct device *dev)
{
	/* @todo Should this do something? Should it free the PCD?
	 */
#if defined(DEBUG) || defined(ISOC_DEBUG)
	printk(KERN_DEBUG USB3_DWC "%s()\n", __func__);
#endif
}


#ifdef DWC_UTE
/*=======================================================================*/
/*
 * UTE support functions
 */

/**
 * Return the PCD pointer for a given gadget instance.
 */
dwc_usb3_pcd_t *dwc_usb3_get_pcd_instance(unsigned devnum)
{
	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		dwc_error(pcd->usb3_dev, "%s, bad devnum %u or null wrapper!\n",
			  __func__, devnum);
		return NULL;
	}

	return gadget_wrapper[devnum]->pcd;
}

/**
 * Set mapping of a USB EP to a physical EP, by changing the contents of the
 * gadget instance's ep_names[] array.
 */
int dwc_usb3_set_usb_ep_map(unsigned devnum, unsigned phys_ep_num,
			    unsigned usb_ep_num)
{
	unsigned usb_ep, usb_dir, usb_type;
	char *dirstr, *typestr, *buf;
	static char *ep_type_str[] = { "ctrl", "iso", "bulk", "int" };

	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		dwc_error(pcd->usb3_dev, "%s, bad devnum %u or null wrapper!\n",
			  __func__, devnum);
		return -ENODEV;
	}

	/* Phys EP 0 & 1 cannot be remapped */
	if (phys_ep_num < 2 || phys_ep_num >= DWC_MAX_PHYS_EP)
		return -EINVAL;

	/* ep_names[] has a single entry for both EP0 IN & OUT */
	phys_ep_num--;

	/* These fields are defined by UTE */
	usb_ep = usb_ep_num & 0xf;
	usb_dir = (usb_ep_num >> 4) & 0x3;
	usb_type = (usb_ep_num >> 6) & 0x7;

	/* Cannot remap the default Control EP */
	if (usb_ep == 0)
		return -EINVAL;

	switch (usb_dir) {
	case 0:
		dirstr = "out";
		break;
	case 1:
		dirstr = "in";
		break;
	default:
		/* USB3 does not have bidirectional physical EPs */
		return -EINVAL;
	}

	buf = gadget_wrapper[devnum]->ep_names[phys_ep_num];

	if (usb_type >= 4) {
		snprintf(buf, sizeof(gadget_wrapper[0]->ep_names[0]),
			 "ep%u%s", usb_ep, dirstr);
	} else {
		typestr = ep_type_str[usb_type];
		snprintf(buf, sizeof(gadget_wrapper[0]->ep_names[0]),
			 "ep%u%s-%s", usb_ep, dirstr, typestr);
	}

	return 0;
}

/**
 * Get mapping of a USB EP to a physical EP, by reading the contents of the
 * gadget instance's ep_names[] array.
 */
int dwc_usb3_get_usb_ep_map(unsigned devnum, unsigned phys_ep_num,
			    unsigned *usb_ep_num_ret)
{
	unsigned usb_ep, usb_dir, usb_type;
	char dirstr[16], typestr[16], *dashp, *buf;
	int ret;

	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		dwc_error(pcd->usb3_dev, "%s, bad devnum %u or null wrapper!\n",
			  __func__, devnum);
		return -ENODEV;
	}

	/* Phys EP 0 & 1 cannot be remapped */
	if (phys_ep_num < 2 || phys_ep_num >= DWC_MAX_PHYS_EP)
		return -EINVAL;

	/* ep_names[] has a single entry for both EP0 IN & OUT */
	phys_ep_num--;

	buf = gadget_wrapper[devnum]->ep_names[phys_ep_num];
	printk("phys EP %d, config=\"%s\"\n", phys_ep_num, buf);
	usb_ep=16;
	dirstr[0] = 0;
	typestr[0] = 0;
	dashp = strchr(buf, '-');
	if (dashp) {
		*dashp = ' ';
		ret = sscanf(buf, "ep%u%15s %15s", &usb_ep, dirstr, typestr);
		*dashp = '-';
		printk("sscanf() ret=%d\n", ret);
		if (ret != 3)
			return -EINVAL;
	} else {
		ret = sscanf(buf, "ep%u%15s", &usb_ep, dirstr);
		printk("sscanf() ret=%d\n", ret);
		if (ret != 2)
			return -EINVAL;
	}

	if (usb_ep > 15)
		return -ERANGE;

	if (strcmp(dirstr, "out") == 0)
		usb_dir = 0;
	else if (strcmp(dirstr, "in") == 0)
		usb_dir = 1;
	else
		return -ERANGE;

	if (strcmp(typestr, "ctrl") == 0)
		usb_type = 0;
	else if (strcmp(typestr, "iso") == 0)
		usb_type = 1;
	else if (strcmp(typestr, "bulk") == 0)
		usb_type = 2;
	else if (strcmp(typestr, "int") == 0)
		usb_type = 3;
	else
		usb_type = 4;

	*usb_ep_num_ret = usb_ep | (usb_dir << 4) | (usb_type << 6);
	return 0;
}

/**
 * Activate the changes made by the UTE to the core's features.
 */
void dwc_usb3_ute_config(dwc_usb3_device_t *usb3_dev)
{
	dwc_usb3_pcd_t *pcd = &usb3_dev->pcd;
	unsigned devnum = pcd->devnum;
	struct gadget_wrapper *wrapper;
	int i, cnt, txsz[DWC_MAX_TX_FIFOS];

	dwc_debug(usb3_dev, "%s()\n", __func__);
	dwc_debug(usb3_dev, "pcd->devnum=%u\n", devnum);

	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		dwc_warn(usb3_dev, "%s, bad devnum %u or null wrapper!\n",
			 __func__, devnum);
		return;
	}

	wrapper = gadget_wrapper[devnum];

	if (pcd->ute_change) {
		/* Free any remaining descriptor allocations made for
		 * non-EP0 EPs
		 */
		gadget_free_ep_allocations(pcd, wrapper);

		/* Set the Tx FIFO sizes */
		for (i = 0, cnt = 0; i < DWC_MAX_TX_FIFOS; i++) {
			txsz[i] = pcd->txf_size[i];
			if (txsz[i])
				cnt++;
		}
		if (cnt)
			dwc_usb3_set_tx_fifo_size(usb3_dev, txsz);

		/* Set the Rx FIFO size */
		if (pcd->rxf_size)
			dwc_usb3_set_rx_fifo_size(usb3_dev, pcd->rxf_size);

		/* Re-initialize non-EP0 EPs to pick up any mapping changes */
		if (gadget_init_eps(pcd, wrapper)) {
			dwc_error(usb3_dev, "%s, gadget_init_eps error!\n",
				  __func__);
		}

		pcd->ute_change = 0;
	}
}

/**
 * Reset usb endpoint mapping to it's default state.
 */
int dwc_usb3_reset_usb_ep_map(unsigned devnum)
{
	struct gadget_wrapper *d;
	dwc_usb3_pcd_t *pcd;
	int i;

	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		dwc_error(pcd->usb3_dev, "%s, bad devnum %u or null wrapper!\n",
			  __func__, devnum);
		return -ENODEV;
	}

	d = gadget_wrapper[devnum];
	pcd = d->pcd;

	for (i = 0; i < ARRAY_SIZE(g_ep_names); i++) {
		strncpy(d->ep_names[i], g_ep_names[i],
			sizeof(d->ep_names[0]) - 1);
		d->ep_names[i][sizeof(d->ep_names[0]) - 1] = 0;
		dwc_debug(usb3_dev, "~phys EP%d name=%s\n", i, d->ep_names[i]);
	}

	return gadget_init_eps(pcd, d);
}

int dwc_usb3_switch_speed(unsigned devnum, int speed)
{
	int ret = 0;
	struct gadget_wrapper *d;
	dwc_usb3_pcd_t *pcd;
	dwc_usb3_device_t *usb3_dev;
	//uint32_t temp;

	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		dwc_error(pcd->usb3_dev, "%s, bad devnum %u or null wrapper!\n",
			  __func__, devnum);
		return -ENODEV;
	}

	d = gadget_wrapper[devnum];
	pcd = d->pcd;
	usb3_dev = pcd->usb3_dev;
	/*
	temp = dwc_rd32(usb3_dev, &pcd->dev_global_regs->dctl);
	temp &= ~DWC_DCTL_RUN_STOP_BIT;
	dwc_wr32(usb3_dev, &pcd->dev_global_regs->dctl, temp);

	dwc_udelay(usb3_dev, 1500); */
	/* Soft-reset the core */
	/* temp = dwc_rd32(usb3_dev, &pcd->dev_global_regs->dctl);
	temp &= ~DWC_DCTL_RUN_STOP_BIT;
	temp |= DWC_DCTL_CSFT_RST_BIT;
	dwc_wr32(usb3_dev, &pcd->dev_global_regs->dctl, temp); */

	/* Wait for core to come out of reset */
	/* do {
		dwc_udelay(usb3_dev, 1);
		temp = dwc_rd32(usb3_dev, &pcd->dev_global_regs->dctl);
	} while (temp & DWC_DCTL_CSFT_RST_BIT);

	dwc_mdelay(usb3_dev, 2); */

	if (speed == 3) {
		/* Set device speed feature to Super Speed */
		speed = 0;
	}
	usb3_dev->core_params->usb2mode = speed;

	dwc_usb3_core_device_init(usb3_dev, 1, 0);

	return ret;
}

int dwc_usb3_get_dev_speed(unsigned devnum)
{
	int ret;
	struct gadget_wrapper *d;
	dwc_usb3_pcd_t *pcd;
	dwc_usb3_device_t *usb3_dev;
	//uint32_t temp;

	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		dwc_error(pcd->usb3_dev, "%s, bad devnum %u or null wrapper!\n",
			  __func__, devnum);
		return -ENODEV;
	}

	d = gadget_wrapper[devnum];
	pcd = d->pcd;
	usb3_dev = pcd->usb3_dev;

	ret = usb3_dev->core_params->usb2mode;
	if (ret == 0) {
		/* If speed feature is set to Super Speed */
		ret = 3;
	}
	return ret;
}

#endif /* DWC_UTE */

/**
 * This function initializes the PCD portion of the driver.
 *
 * @param dev The device context.
 */
int dwc_usb3_init(
	struct platform_device *dev
	)
{
	static char *pcd_name[4] = { "dwc_usb3_pcd", "dwc_usb3_pcd1",
				     "dwc_usb3_pcd2", "dwc_usb3_pcd3" };
	dwc_usb3_device_t *usb3_dev;// = (dwc_usb3_device_t*)platform_get_dwc3data(dev);

	struct gadget_wrapper *wrapper;
	dwc_usb3_pcd_t *pcd;// = &usb3_dev->pcd;
	unsigned devnum;// = pcd->devnum;
	int retval = -ENOMEM;
	int hiberbufs = 0;
	dma_addr_t dma_addr;
	int i;


	usb3_dev = (dwc_usb3_device_t*)platform_get_dwc3data(dev);
	pcd = &usb3_dev->pcd;
	devnum = pcd->devnum;

	dwc_debug(usb3_dev, "%s()\n", __func__);
	dwc_debug(usb3_dev, "pcd=%p\n", pcd);
	dwc_debug(usb3_dev, "pcd->devnum=%u\n", devnum);

	
	wrapper = kmalloc(sizeof(struct gadget_wrapper), GFP_KERNEL);
	if (!wrapper)
		goto out1;

	gadget_wrapper[devnum] = wrapper;
	memset(wrapper, 0, sizeof(*wrapper));
	wrapper->pcd = pcd;

	for (i = 0; i < ARRAY_SIZE(g_ep_names); i++) {
		strncpy(wrapper->ep_names[i], g_ep_names[i],
			sizeof(wrapper->ep_names[0]) - 1);
		wrapper->ep_names[i][sizeof(wrapper->ep_names[0]) - 1] = 0;
		dwc_debug(usb3_dev, "~phys EP%d name=%s\n", i,
			  wrapper->ep_names[i]);
	}

	wrapper->gadget.name = pcd_name[devnum];
	dwc_debug(usb3_dev, "gadget.name=%s\n", pcd_name[devnum]);

#ifdef CONFIG_USB_OTG_DWC
	wrapper->gadget.is_otg = 1;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)
	strcpy(wrapper->gadget.dev.bus_id, "gadget");
#else
	dev_set_name(&wrapper->gadget.dev, "%s", "gadget");
#endif
	wrapper->gadget.dev.parent = &dev->dev;
	wrapper->gadget.dev.release = gadget_release;
	wrapper->gadget.speed = USB_SPEED_UNKNOWN;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0)
	wrapper->gadget.is_dualspeed = 1;
#else
	//wrapper->gadget.max_speed = USB_SPEED_SUPER; //taejin
	wrapper->gadget.speed = USB_SPEED_SUPER; //taejin
#endif
	wrapper->gadget.ops = &pcd_ops;
	wrapper->driver = NULL;
	dwc_debug(usb3_dev, "gadget=%p ops=%p\n", &wrapper->gadget,
		  wrapper->gadget.ops);

	/* Set the PCD's EP0 request pointer to the wrapper's request */
	pcd->ep0_req = &wrapper->ep0_req;

	/* Set the PCD's EP array pointers to the wrapper's EPs */
	pcd->ep0 = &wrapper->ep0;
	for (i = 0; i < DWC_MAX_EPS - 1; i++) {
		pcd->out_ep[i] = &wrapper->out_ep[i];
		pcd->in_ep[i] = &wrapper->in_ep[i];
	}

	/* Allocate the EP0 packet buffers */
	pcd->ep0_setup_pkt = dma_alloc_coherent(NULL,
						sizeof(*pcd->ep0_setup_pkt) * 5,
						&pcd->ep0_setup_pkt_dma,
						GFP_KERNEL | GFP_DMA);
	if (!pcd->ep0_setup_pkt)
		goto out2;
	pcd->ep0_status_buf = dma_alloc_coherent(NULL, DWC_STATUS_BUF_SIZE,
						 &pcd->ep0_status_buf_dma,
						 GFP_KERNEL | GFP_DMA);
	if (!pcd->ep0_status_buf)
		goto out3;

	/* Allocate the EP0 DMA descriptors */
	pcd->ep0_setup_desc = dma_alloc_coherent(NULL,
						 sizeof(dwc_usb3_dma_desc_t),
						 &pcd->ep0_setup_desc_dma,
						 GFP_KERNEL | GFP_DMA);
	if (!pcd->ep0_setup_desc)
		goto out4;
	pcd->ep0_in_desc = dma_alloc_coherent(NULL, sizeof(dwc_usb3_dma_desc_t),
					      &pcd->ep0_in_desc_dma,
					      GFP_KERNEL | GFP_DMA);
	if (!pcd->ep0_in_desc)
		goto out5;
	pcd->ep0_out_desc = dma_alloc_coherent(NULL,
					       sizeof(dwc_usb3_dma_desc_t),
					       &pcd->ep0_out_desc_dma,
					       GFP_KERNEL | GFP_DMA);
	if (!pcd->ep0_out_desc)
		goto out6;

	/* If hibernation is supported */
	if (usb3_dev->core_params->hibernate &&
	    (usb3_dev->hwparams1 & DWC_HWP1_EN_PWROPT_BITS) ==
	    (DWC_EN_PWROPT_HIBERNATION << DWC_HWP1_EN_PWROPT_SHIFT)) {
		hiberbufs = (usb3_dev->hwparams4 >> DWC_HWP4_HIBER_SPAD_SHIFT) &
			(DWC_HWP4_HIBER_SPAD_BITS >> DWC_HWP4_HIBER_SPAD_SHIFT);
		if (hiberbufs) {
			/* Allocate scratch buffer pointer array */
			pcd->hiber_scratchpad_array =
				dma_alloc_coherent(NULL,
					sizeof(*pcd->hiber_scratchpad_array),
					&pcd->hiber_scratchpad_array_dma,
					GFP_KERNEL | GFP_DMA);
			if (!pcd->hiber_scratchpad_array) {
				dwc_debug(usb3_dev,
				      "%s hibernation array allocation error\n",
				      __func__);
				goto out7;
			}
		}

		/* Allocate scratch buffers */
		for (i = 0; i < hiberbufs; i++) {
			pcd->hiber_scratchpad[i] =
				dma_alloc_coherent(NULL, 4096, &dma_addr,
						   GFP_KERNEL | GFP_DMA);
			if (!pcd->hiber_scratchpad[i]) {
				dwc_debug(usb3_dev,
					"%s hibernation buf allocation error\n",
					__func__);
				while (i-- > 0) {
					dma_addr = (dma_addr_t)pcd->
						hiber_scratchpad_array->
								dma_addr[i];
					dma_free_coherent(NULL, 4096,
						pcd->hiber_scratchpad[i],
						dma_addr);
					pcd->hiber_scratchpad[i] = NULL;
				}

				goto out8;
			}

			pcd->hiber_scratchpad_array->dma_addr[i] =
							(uint64_t)dma_addr;
		}
	}

	/* Allocate the tasklet */
	tasklet_init(&pcd->test_mode_tasklet, dwc_usb3_do_test_mode,
		     (unsigned long)pcd);

	/* Init the PCD (also enables interrupts and sets Run/Stop bit) */
	retval = dwc_usb3_pcd_init(usb3_dev);
	if (retval) {
		dwc_debug(usb3_dev, "%s dwc_usb3_pcd_init error\n", __func__);
		goto out9;
	}

	/* Initialize all the EP structures */
	gadget_init_ep0(pcd, wrapper);
	retval = gadget_init_eps(pcd, wrapper);
	if (retval) {
		dwc_debug(usb3_dev, "%s gadget_init_eps error\n", __func__);
		goto out10;
	}

	/* Register the gadget device */
	retval = device_register(&wrapper->gadget.dev);
	if (retval) {
		dwc_debug(usb3_dev, "%s cannot register gadget device\n",
			  __func__);
		goto out11;
	}

	return 0;

out11:
	gadget_free_ep_allocations(pcd, wrapper);
out10:
	dwc_usb3_pcd_remove(usb3_dev);
out9:
	for (i = hiberbufs - 1; i >= 0; i--) {
		if (pcd->hiber_scratchpad[i]) {
			dma_addr = (dma_addr_t)
				pcd->hiber_scratchpad_array->dma_addr[i];
			dma_free_coherent(NULL, 4096, pcd->hiber_scratchpad[i],
					  dma_addr);
			pcd->hiber_scratchpad[i] = NULL;
		}
	}
out8:
	if (hiberbufs)
		dma_free_coherent(NULL, sizeof(*pcd->hiber_scratchpad_array),
				  pcd->hiber_scratchpad_array,
				  pcd->hiber_scratchpad_array_dma);
out7:
	dma_free_coherent(NULL, sizeof(dwc_usb3_dma_desc_t), pcd->ep0_out_desc,
			  pcd->ep0_out_desc_dma);
out6:
	dma_free_coherent(NULL, sizeof(dwc_usb3_dma_desc_t), pcd->ep0_in_desc,
			  pcd->ep0_in_desc_dma);
out5:
	dma_free_coherent(NULL, sizeof(dwc_usb3_dma_desc_t),
			  pcd->ep0_setup_desc, pcd->ep0_setup_desc_dma);
out4:
	dma_free_coherent(NULL, DWC_STATUS_BUF_SIZE, pcd->ep0_status_buf,
			  pcd->ep0_status_buf_dma);
out3:
	dma_free_coherent(NULL, sizeof(*pcd->ep0_setup_pkt) * 5,
			  pcd->ep0_setup_pkt, pcd->ep0_setup_pkt_dma);
out2:
	gadget_wrapper[devnum] = NULL;
	kfree(wrapper);
out1:
	return retval;
}

/**
 * Cleanup the PCD.
 *
 * @param dev The device context.
 */
void dwc_usb3_remove(
	struct platform_device *dev
	)
{
	dwc_usb3_device_t *usb3_dev = platform_get_drvdata(dev);

	struct gadget_wrapper *wrapper;
	dwc_usb3_pcd_t *pcd = &usb3_dev->pcd;
	unsigned devnum = pcd->devnum;
	int hiberbufs, i;
	void *addr;
	dma_addr_t dma_addr;

	dwc_debug(usb3_dev, "%s()\n", __func__);
	dwc_debug(usb3_dev, "pcd->devnum=%u\n", devnum);

	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum]) {
		dwc_warn(usb3_dev, "%s, bad devnum %u or null wrapper!\n",
			 __func__, devnum);
		return;
	}

	wrapper = gadget_wrapper[devnum];

	/* start with the driver above us */
	if (wrapper->driver) {
		/* should have been done already by driver model core */
		dwc_warn(usb3_dev, "driver '%s' %u is still registered!\n",
			 wrapper->driver->driver.name, devnum);
		usb_gadget_unregister_driver(wrapper->driver);
	}

	device_unregister(&wrapper->gadget.dev);
	gadget_free_ep_allocations(pcd, wrapper);
	dwc_usb3_pcd_remove(usb3_dev);

	/* If hibernation is supported */
	if (usb3_dev->core_params->hibernate &&
	    (usb3_dev->hwparams1 & DWC_HWP1_EN_PWROPT_BITS) ==
	    (DWC_EN_PWROPT_HIBERNATION << DWC_HWP1_EN_PWROPT_SHIFT)) {
		hiberbufs = (usb3_dev->hwparams4 >> DWC_HWP4_HIBER_SPAD_SHIFT) &
			(DWC_HWP4_HIBER_SPAD_BITS >> DWC_HWP4_HIBER_SPAD_SHIFT);

		/* Free hibernation scratch buffers */
		for (i = hiberbufs - 1; i >= 0; i--) {
			addr = pcd->hiber_scratchpad[i];
			dma_addr = (dma_addr_t)pcd->hiber_scratchpad_array->
								dma_addr[i];
			pcd->hiber_scratchpad[i] = NULL;
			if (addr)
				dma_free_coherent(NULL, 4096, addr, dma_addr);
		}

		if (hiberbufs)
			dma_free_coherent(NULL,
					  sizeof(*pcd->hiber_scratchpad_array),
					  pcd->hiber_scratchpad_array,
					  pcd->hiber_scratchpad_array_dma);
	}

	dma_free_coherent(NULL, sizeof(dwc_usb3_dma_desc_t), pcd->ep0_out_desc,
			  pcd->ep0_out_desc_dma);
	dma_free_coherent(NULL, sizeof(dwc_usb3_dma_desc_t), pcd->ep0_in_desc,
			  pcd->ep0_in_desc_dma);
	dma_free_coherent(NULL, sizeof(dwc_usb3_dma_desc_t), pcd->ep0_setup_desc,
			  pcd->ep0_setup_desc_dma);
	dma_free_coherent(NULL, DWC_STATUS_BUF_SIZE, pcd->ep0_status_buf,
			  pcd->ep0_status_buf_dma);
	dma_free_coherent(NULL, sizeof(*pcd->ep0_setup_pkt) * 5,
			  pcd->ep0_setup_pkt, pcd->ep0_setup_pkt_dma);

	gadget_wrapper[devnum] = NULL;
	kfree(wrapper);
}

/**
 * This function registers a gadget driver with the PCD.
 *
 * When a driver is successfully registered, it will receive control
 * requests including set_configuration(), which enables non-control
 * requests. Then usb traffic follows until a disconnect is reported.
 * Then a host may connect again, or the driver might get unbound.
 *
 * @param driver The driver being registered.
 * @param bind   The gadget driver's bind function.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
int usb_gadget_register_driver(struct usb_gadget_driver *driver)
#else
int usb_gadget_probe_driver(struct usb_gadget_driver *driver,
			    int (*bind)(struct usb_gadget *))
#endif
{
#ifdef DWC_MULTI_GADGET
	unsigned devnum = driver->devnum;
#else
	unsigned devnum = 0;
#endif
	struct gadget_wrapper *wrapper;
	dwc_usb3_pcd_t *pcd;
	int retval;
#ifdef CONFIG_USB_OTG_DWC
	struct otg_transceiver *otg;
#endif

	printk(KERN_DEBUG USB3_DWC "%s(%p)\n", __func__, driver);
	printk(KERN_DEBUG USB3_DWC "dr->devnum=%u\n", devnum);

	if (devnum >= FSTOR_NUM_GADGETS)
		return -ENODEV;

	printk(KERN_DEBUG USB3_DWC "gdt_wrp[dr->devnum]=%p\n",
	       gadget_wrapper[devnum]);

	if (!gadget_wrapper[devnum])
		return -ENODEV;

	wrapper = gadget_wrapper[devnum];
	pcd = wrapper->pcd;
	printk(KERN_DEBUG USB3_DWC "pcd=%p\n", pcd);

	if (!pcd) {
		printk(KERN_DEBUG USB3_DWC "ENODEV\n");
		return -ENODEV;
	}

	if (!driver ||
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0)
	    driver->speed == USB_SPEED_UNKNOWN ||
#else
	    //driver->max_speed == USB_SPEED_UNKNOWN ||
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
	    !driver->bind ||
#endif
	    !driver->unbind ||
	    !driver->disconnect || !driver->setup) {
		printk(KERN_DEBUG USB3_DWC "EINVAL\n");
		return -EINVAL;
	}

	printk(KERN_DEBUG USB3_DWC "registering gadget driver '%s'\n",
	       driver->driver.name);

	if (wrapper->driver) {
		printk(KERN_DEBUG USB3_DWC "EBUSY (%p)\n", wrapper->driver);
		return -EBUSY;
	}

#ifdef CONFIG_USB_OTG_DWC
	/* Check that the otg transceiver driver is loaded */
	otg = otg_get_transceiver();
	if (!otg) {
		printk(KERN_DEBUG USB3_DWC "OTG driver not available!\n");
		return -ENODEV;
	}

	otg_put_transceiver(otg);
#endif

	/* hook up the driver */
	wrapper->driver = driver;
	wrapper->gadget.dev.driver = &driver->driver;
	pcd->gadget = &wrapper->gadget;

#ifdef DWC_MULTI_GADGET
	wrapper->gadget.devnum = devnum;
#endif
	dwc_debug(pcd->usb3_dev, "bind to driver %s %u\n",
		  driver->driver.name, devnum);
	dwc_debug(pcd->usb3_dev, "&gadget_wrapper->gadget = %p\n",
		  &wrapper->gadget);
	dwc_debug(pcd->usb3_dev, "gadget_wrapper->driver = %p\n",
		  wrapper->driver);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
	retval = driver->bind(&wrapper->gadget);
#else
	retval = bind(&wrapper->gadget);
#endif
	if (retval) {
		dwc_error(pcd->usb3_dev, "bind to driver %s --> error %d\n",
			  driver->driver.name, retval);
		wrapper->driver = NULL;
		wrapper->gadget.dev.driver = NULL;
		pcd->gadget = NULL;

#ifdef DWC_MULTI_GADGET
		wrapper->gadget.devnum = 0;
		pcd->devnum = 0;
#endif
		return retval;
	}

#ifdef CONFIG_USB_OTG_DWC
	otg = otg_get_transceiver();
	otg->io_priv = (uint8_t *)pcd->dev_global_regs - 0xc700;
	printk(KERN_DEBUG USB3_DWC "Setting io_priv=%p\n", otg->io_priv);
	printk(USB3_DWC "  c120=%x\n",
	       *(uint32_t *)((uint8_t *)otg->io_priv + 0xc120));
	otg_set_peripheral(otg, &wrapper->gadget);
	otg_put_transceiver(otg);
#endif

	printk(KERN_DEBUG USB3_DWC "registered gadget driver '%s'\n",
	       driver->driver.name);
	return 0;
}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
EXPORT_SYMBOL(usb_gadget_register_driver);
#else
EXPORT_SYMBOL(usb_gadget_probe_driver);
#endif

/**
 * This function unregisters a gadget driver
 *
 * @param driver The driver being unregistered.
 */
int usb_gadget_unregister_driver(struct usb_gadget_driver *driver)
{
#ifdef DWC_MULTI_GADGET
	unsigned devnum = driver->devnum;
#else
	unsigned devnum = 0;
#endif
	struct gadget_wrapper *wrapper;

#ifdef CONFIG_USB_OTG_DWC
	struct otg_transceiver *otg;
#endif

	printk(KERN_DEBUG USB3_DWC "%s(%p)\n", __func__, driver);
	printk(KERN_DEBUG USB3_DWC "dr->devnum=%u\n", devnum);
	printk(KERN_DEBUG USB3_DWC "unregistering gadget driver '%s'\n",
	       driver->driver.name);

	if (devnum >= FSTOR_NUM_GADGETS || !gadget_wrapper[devnum])
		return -ENODEV;

	wrapper = gadget_wrapper[devnum];

	if (!wrapper->pcd) {
		printk(KERN_DEBUG USB3_DWC "%s Return(%d): pcd==NULL\n",
		       __func__, -ENODEV);
		return -ENODEV;
	}

	if (!wrapper->driver || driver != wrapper->driver) {
		printk(KERN_DEBUG USB3_DWC "%s Return(%d): driver?\n",
		       __func__, -EINVAL);
		return -EINVAL;
	}

#ifdef CONFIG_USB_OTG_DWC
	otg = otg_get_transceiver();
	otg_set_peripheral(otg, NULL);
	otg_put_transceiver(otg);
#endif

	driver->disconnect(&wrapper->gadget);
	driver->unbind(&wrapper->gadget);
	wrapper->driver = NULL;
	wrapper->gadget.dev.driver = NULL;
	wrapper->pcd->gadget = NULL;

#ifdef DWC_MULTI_GADGET
	wrapper->gadget.devnum = 0;
	wrapper->pcd->devnum = 0;
#endif
	printk(KERN_DEBUG USB3_DWC "unregistered gadget driver '%s'\n",
	       driver->driver.name);
	return 0;
}
EXPORT_SYMBOL(usb_gadget_unregister_driver);
