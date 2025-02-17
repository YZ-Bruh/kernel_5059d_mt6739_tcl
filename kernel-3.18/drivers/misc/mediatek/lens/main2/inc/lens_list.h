
#ifndef _LENS_LIST_H

#define _LENS_LIST_H

#define AK7371AF_SetI2Cclient AK7371AF_SetI2Cclient_Main2
#define AK7371AF_Ioctl AK7371AF_Ioctl_Main2
#define AK7371AF_Release AK7371AF_Release_Main2
#define AK7371AF_PowerDown AK7371AF_PowerDown_Main2
extern int AK7371AF_SetI2Cclient(struct i2c_client *pstAF_I2Cclient, spinlock_t *pAF_SpinLock, int *pAF_Opened);
extern long AK7371AF_Ioctl(struct file *a_pstFile, unsigned int a_u4Command, unsigned long a_u4Param);
extern int AK7371AF_Release(struct inode *a_pstInode, struct file *a_pstFile);
extern int AK7371AF_PowerDown(void);

#define LC898212XDAF_F_SetI2Cclient LC898212XDAF_F_SetI2Cclient_Main2
#define LC898212XDAF_F_Ioctl LC898212XDAF_F_Ioctl_Main2
#define LC898212XDAF_F_Release LC898212XDAF_F_Release_Main2
extern int LC898212XDAF_F_SetI2Cclient(struct i2c_client *pstAF_I2Cclient, spinlock_t *pAF_SpinLock, int *pAF_Opened);
extern long LC898212XDAF_F_Ioctl(struct file *a_pstFile, unsigned int a_u4Command, unsigned long a_u4Param);
extern int LC898212XDAF_F_Release(struct inode *a_pstInode, struct file *a_pstFile);

#endif
