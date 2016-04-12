
/*
 * drm_sysfs.c - Modifications to drm_sysfs_class.c to support
 *               extra sysfs attribute from DRM. Normal drm_sysfs_class
 *               does not allow adding attributes.
 *
 * Copyright (c) 2004 Jon Smirl <jonsmirl@gmail.com>
 * Copyright (c) 2003-2004 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (c) 2003-2004 IBM Corp.
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/gfp.h>
#include <linux/err.h>
#include <linux/export.h>

#include <drm/drm_sysfs.h>
#include <drm/drm_core.h>
#include <drm/drmP.h>
#include "drm_internal.h"

#define to_drm_minor(d) dev_get_drvdata(d)
#define to_drm_connector(d) dev_get_drvdata(d)

static struct device_type drm_sysfs_device_minor = {
	.name = "drm_minor"
};

struct class *drm_class;

/**
 * __drm_class_suspend - internal DRM class suspend routine
 * @dev: Linux device to suspend
 * @state: power state to enter
 *
 * Just figures out what the actual struct drm_device associated with
 * @dev is and calls its suspend hook, if present.
 */
static int __drm_class_suspend(struct device *dev, pm_message_t state)
{
	if (dev->type == &drm_sysfs_device_minor) {
		struct drm_minor *drm_minor = to_drm_minor(dev);
		struct drm_device *drm_dev = drm_minor->dev;

		if (drm_minor->type == DRM_MINOR_LEGACY &&
		    !drm_core_check_feature(drm_dev, DRIVER_MODESET) &&
		    drm_dev->driver->suspend)
			return drm_dev->driver->suspend(drm_dev, state);
	}
	return 0;
}

/**
 * drm_class_suspend - internal DRM class suspend hook. Simply calls
 * __drm_class_suspend() with the correct pm state.
 * @dev: Linux device to suspend
 */
static int drm_class_suspend(struct device *dev)
{
	return __drm_class_suspend(dev, PMSG_SUSPEND);
}

/**
 * drm_class_freeze - internal DRM class freeze hook. Simply calls
 * __drm_class_suspend() with the correct pm state.
 * @dev: Linux device to freeze
 */
static int drm_class_freeze(struct device *dev)
{
	return __drm_class_suspend(dev, PMSG_FREEZE);
}

/**
 * drm_class_resume - DRM class resume hook
 * @dev: Linux device to resume
 *
 * Just figures out what the actual struct drm_device associated with
 * @dev is and calls its resume hook, if present.
 */
static int drm_class_resume(struct device *dev)
{
	if (dev->type == &drm_sysfs_device_minor) {
		struct drm_minor *drm_minor = to_drm_minor(dev);
		struct drm_device *drm_dev = drm_minor->dev;

		if (drm_minor->type == DRM_MINOR_LEGACY &&
		    !drm_core_check_feature(drm_dev, DRIVER_MODESET) &&
		    drm_dev->driver->resume)
			return drm_dev->driver->resume(drm_dev);
	}
	return 0;
}

static const struct dev_pm_ops drm_class_dev_pm_ops = {
	.suspend	= drm_class_suspend,
	.resume		= drm_class_resume,
	.freeze		= drm_class_freeze,
};

static char *drm_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "dri/%s", dev_name(dev));
}

static CLASS_ATTR_STRING(version, S_IRUGO,
		CORE_NAME " "
		__stringify(CORE_MAJOR) "."
		__stringify(CORE_MINOR) "."
		__stringify(CORE_PATCHLEVEL) " "
		CORE_DATE);

/**
 * drm_sysfs_init - initialize sysfs helpers
 *
 * This is used to create the DRM class, which is the implicit parent of any
 * other top-level DRM sysfs objects.
 *
 * You must call drm_sysfs_destroy() to release the allocated resources.
 *
 * Return: 0 on success, negative error code on failure.
 */
int drm_sysfs_init(void)
{
	int err;

	drm_class = class_create(THIS_MODULE, "drm");
	if (IS_ERR(drm_class))
		return PTR_ERR(drm_class);

	drm_class->pm = &drm_class_dev_pm_ops;

	err = class_create_file(drm_class, &class_attr_version.attr);
	if (err) {
		class_destroy(drm_class);
		drm_class = NULL;
		return err;
	}

	drm_class->devnode = drm_devnode;
	return 0;
}

/**
 * drm_sysfs_destroy - destroys DRM class
 *
 * Destroy the DRM device class.
 */
void drm_sysfs_destroy(void)
{
	if (IS_ERR_OR_NULL(drm_class))
		return;
	class_remove_file(drm_class, &class_attr_version.attr);
	class_destroy(drm_class);
	drm_class = NULL;
}

/*
 * Connector properties
 */
static ssize_t status_store(struct device *device,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_device *dev = connector->dev;
	enum drm_connector_force old_force;
	int ret;

	ret = mutex_lock_interruptible(&dev->mode_config.mutex);
	if (ret)
		return ret;

	old_force = connector->force;

	if (sysfs_streq(buf, "detect"))
		connector->force = 0;
	else if (sysfs_streq(buf, "on"))
		connector->force = DRM_FORCE_ON;
	else if (sysfs_streq(buf, "on-digital"))
		connector->force = DRM_FORCE_ON_DIGITAL;
	else if (sysfs_streq(buf, "off"))
		connector->force = DRM_FORCE_OFF;
	else
		ret = -EINVAL;

	if (old_force != connector->force || !connector->force) {
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] force updated from %d to %d or reprobing\n",
			      connector->base.id,
			      connector->name,
			      old_force, connector->force);

		connector->funcs->fill_modes(connector,
					     dev->mode_config.max_width,
					     dev->mode_config.max_height);
	}

	mutex_unlock(&dev->mode_config.mutex);

	return ret ? ret : count;
}

static ssize_t status_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			drm_get_connector_status_name(connector->status));
}

static ssize_t dpms_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	int dpms;

	dpms = READ_ONCE(connector->dpms);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			drm_get_dpms_name(dpms));
}

static ssize_t enabled_show(struct device *device,
			    struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);

	return snprintf(buf, PAGE_SIZE, "%s\n", connector->encoder ? "enabled" :
			"disabled");
}

static ssize_t edid_show(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *attr, char *buf, loff_t off,
			 size_t count)
{
	struct device *connector_dev = kobj_to_dev(kobj);
	struct drm_connector *connector = to_drm_connector(connector_dev);
	unsigned char *edid;
	size_t size;
	ssize_t ret = 0;

	mutex_lock(&connector->dev->mode_config.mutex);
	if (!connector->edid_blob_ptr)
		goto unlock;

	edid = connector->edid_blob_ptr->data;
	size = connector->edid_blob_ptr->length;
	if (!edid)
		goto unlock;

	if (off >= size)
		goto unlock;

	if (off + count > size)
		count = size - off;
	memcpy(buf, edid + off, count);

	ret = count;
unlock:
	mutex_unlock(&connector->dev->mode_config.mutex);

	return ret;
}

static ssize_t modes_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_display_mode *mode;
	int written = 0;

	mutex_lock(&connector->dev->mode_config.mutex);
	list_for_each_entry(mode, &connector->modes, head) {
		written += snprintf(buf + written, PAGE_SIZE - written, "%s\n",
				    mode->name);
	}
	mutex_unlock(&connector->dev->mode_config.mutex);

	return written;
}

static ssize_t tv_subconnector_show(struct device *device,
				    struct device_attribute *attr,
				    char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_device *dev = connector->dev;
	struct drm_property *prop;
	uint64_t subconnector;
	int ret;

	prop = dev->mode_config.tv_subconnector_property;
	if (!prop) {
		DRM_ERROR("Unable to find subconnector property\n");
		return 0;
	}

	ret = drm_object_property_get_value(&connector->base, prop, &subconnector);
	if (ret)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%s",
			drm_get_tv_subconnector_name((int)subconnector));
}

static ssize_t tv_select_subconnector_show(struct device *device,
					   struct device_attribute *attr,
					   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_device *dev = connector->dev;
	struct drm_property *prop;
	uint64_t subconnector;
	int ret;

	prop = dev->mode_config.tv_select_subconnector_property;
	if (!prop) {
		DRM_ERROR("Unable to find select subconnector property\n");
		return 0;
	}

	ret = drm_object_property_get_value(&connector->base, prop, &subconnector);
	if (ret)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%s",
			drm_get_tv_select_name((int)subconnector));
}

static ssize_t dvii_subconnector_show(struct device *device,
				      struct device_attribute *attr,
				      char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_device *dev = connector->dev;
	struct drm_property *prop;
	uint64_t subconnector;
	int ret;

	prop = dev->mode_config.dvi_i_subconnector_property;
	if (!prop) {
		DRM_ERROR("Unable to find subconnector property\n");
		return 0;
	}

	ret = drm_object_property_get_value(&connector->base, prop, &subconnector);
	if (ret)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%s",
			drm_get_dvi_i_subconnector_name((int)subconnector));
}

static ssize_t dvii_select_subconnector_show(struct device *device,
					     struct device_attribute *attr,
					     char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_device *dev = connector->dev;
	struct drm_property *prop;
	uint64_t subconnector;
	int ret;

	prop = dev->mode_config.dvi_i_select_subconnector_property;
	if (!prop) {
		DRM_ERROR("Unable to find select subconnector property\n");
		return 0;
	}

	ret = drm_object_property_get_value(&connector->base, prop, &subconnector);
	if (ret)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%s",
			drm_get_dvi_i_select_name((int)subconnector));
}

static DEVICE_ATTR_RW(status);
static DEVICE_ATTR_RO(enabled);
static DEVICE_ATTR_RO(dpms);
static DEVICE_ATTR_RO(modes);

static struct attribute *connector_dev_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_enabled.attr,
	&dev_attr_dpms.attr,
	&dev_attr_modes.attr,
	NULL
};

static DEVICE_ATTR_RO(tv_subconnector);
static DEVICE_ATTR_RO(tv_select_subconnector);

static struct attribute *connector_tv_dev_attrs[] = {
	&dev_attr_tv_subconnector.attr,
	&dev_attr_tv_select_subconnector.attr,
	NULL
};

static DEVICE_ATTR_RO(dvii_subconnector);
static DEVICE_ATTR_RO(dvii_select_subconnector);

static struct attribute *connector_dvii_dev_attrs[] = {
	&dev_attr_dvii_subconnector.attr,
	&dev_attr_dvii_select_subconnector.attr,
	NULL
};

/* Connector type related helpers */
static int kobj_connector_type(struct kobject *kobj)
{
	struct device *dev = kobj_to_dev(kobj);
	struct drm_connector *connector = to_drm_connector(dev);

	return connector->connector_type;
}

static umode_t connector_is_dvii(struct kobject *kobj,
				 struct attribute *attr, int idx)
{
	return kobj_connector_type(kobj) == DRM_MODE_CONNECTOR_DVII ?
		attr->mode : 0;
}

static umode_t connector_is_tv(struct kobject *kobj,
			       struct attribute *attr, int idx)
{
	switch (kobj_connector_type(kobj)) {
	case DRM_MODE_CONNECTOR_Composite:
	case DRM_MODE_CONNECTOR_SVIDEO:
	case DRM_MODE_CONNECTOR_Component:
	case DRM_MODE_CONNECTOR_TV:
		return attr->mode;
	}

	return 0;
}

static struct bin_attribute edid_attr = {
	.attr.name = "edid",
	.attr.mode = 0444,
	.size = 0,
	.read = edid_show,
};

static struct bin_attribute *connector_bin_attrs[] = {
	&edid_attr,
	NULL
};

static const struct attribute_group connector_dev_group = {
	.attrs = connector_dev_attrs,
	.bin_attrs = connector_bin_attrs,
};

static const struct attribute_group connector_tv_dev_group = {
	.attrs = connector_tv_dev_attrs,
	.is_visible = connector_is_tv,
};

static const struct attribute_group connector_dvii_dev_group = {
	.attrs = connector_dvii_dev_attrs,
	.is_visible = connector_is_dvii,
};

static const struct attribute_group *connector_dev_groups[] = {
	&connector_dev_group,
	&connector_tv_dev_group,
	&connector_dvii_dev_group,
	NULL
};

/**
 * drm_sysfs_connector_add - add a connector to sysfs
 * @connector: connector to add
 *
 * Create a connector device in sysfs, along with its associated connector
 * properties (so far, connection status, dpms, mode list & edid) and
 * generate a hotplug event so userspace knows there's a new connector
 * available.
 */
int drm_sysfs_connector_add(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	if (connector->kdev)
		return 0;

	connector->kdev =
		device_create_with_groups(drm_class, dev->primary->kdev, 0,
					  connector, connector_dev_groups,
					  "card%d-%s", dev->primary->index,
					  connector->name);
	DRM_DEBUG("adding \"%s\" to sysfs\n",
		  connector->name);

	if (IS_ERR(connector->kdev)) {
		DRM_ERROR("failed to register connector device: %ld\n", PTR_ERR(connector->kdev));
		return PTR_ERR(connector->kdev);
	}

	/* Let userspace know we have a new connector */
	drm_sysfs_hotplug_event(dev);

	return 0;
}

/**
 * drm_sysfs_connector_remove - remove an connector device from sysfs
 * @connector: connector to remove
 *
 * Remove @connector and its associated attributes from sysfs.  Note that
 * the device model core will take care of sending the "remove" uevent
 * at this time, so we don't need to do it.
 *
 * Note:
 * This routine should only be called if the connector was previously
 * successfully registered.  If @connector hasn't been registered yet,
 * you'll likely see a panic somewhere deep in sysfs code when called.
 */
void drm_sysfs_connector_remove(struct drm_connector *connector)
{
	if (!connector->kdev)
		return;
	DRM_DEBUG("removing \"%s\" from sysfs\n",
		  connector->name);

	device_unregister(connector->kdev);
	connector->kdev = NULL;
}

/**
 * drm_sysfs_hotplug_event - generate a DRM uevent
 * @dev: DRM device
 *
 * Send a uevent for the DRM device specified by @dev.  Currently we only
 * set HOTPLUG=1 in the uevent environment, but this could be expanded to
 * deal with other types of events.
 */
void drm_sysfs_hotplug_event(struct drm_device *dev)
{
	char *event_string = "HOTPLUG=1";
	char *envp[] = { event_string, NULL };

	DRM_DEBUG("generating hotplug event\n");

	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL(drm_sysfs_hotplug_event);

static void drm_sysfs_release(struct device *dev)
{
	kfree(dev);
}

/**
 * drm_sysfs_minor_alloc() - Allocate sysfs device for given minor
 * @minor: minor to allocate sysfs device for
 *
 * This allocates a new sysfs device for @minor and returns it. The device is
 * not registered nor linked. The caller has to use device_add() and
 * device_del() to register and unregister it.
 *
 * Note that dev_get_drvdata() on the new device will return the minor.
 * However, the device does not hold a ref-count to the minor nor to the
 * underlying drm_device. This is unproblematic as long as you access the
 * private data only in sysfs callbacks. device_del() disables those
 * synchronously, so they cannot be called after you cleanup a minor.
 */
struct device *drm_sysfs_minor_alloc(struct drm_minor *minor)
{
	const char *minor_str;
	struct device *kdev;
	int r;

	if (minor->type == DRM_MINOR_CONTROL)
		minor_str = "controlD%d";
	else if (minor->type == DRM_MINOR_RENDER)
		minor_str = "renderD%d";
	else
		minor_str = "card%d";

	kdev = kzalloc(sizeof(*kdev), GFP_KERNEL);
	if (!kdev)
		return ERR_PTR(-ENOMEM);

	device_initialize(kdev);
	kdev->devt = MKDEV(DRM_MAJOR, minor->index);
	kdev->class = drm_class;
	kdev->type = &drm_sysfs_device_minor;
	kdev->parent = minor->dev->dev;
	kdev->release = drm_sysfs_release;
	dev_set_drvdata(kdev, minor);

	r = dev_set_name(kdev, minor_str, minor->index);
	if (r < 0)
		goto err_free;

	return kdev;

err_free:
	put_device(kdev);
	return ERR_PTR(r);
}

/**
 * drm_class_device_register - Register a struct device in the drm class.
 *
 * @dev: pointer to struct device to register.
 *
 * @dev should have all relevant members pre-filled with the exception
 * of the class member. In particular, the device_type member must
 * be set.
 */

int drm_class_device_register(struct device *dev)
{
	if (!drm_class || IS_ERR(drm_class))
		return -ENOENT;

	dev->class = drm_class;
	return device_register(dev);
}
EXPORT_SYMBOL_GPL(drm_class_device_register);

void drm_class_device_unregister(struct device *dev)
{
	return device_unregister(dev);
}
EXPORT_SYMBOL_GPL(drm_class_device_unregister);
