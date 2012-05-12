AC_DEFUN([ACI_PACKAGE],[
  ifelse(ifelse([$4], ,[yes],[$4]),[yes],
          [aci_modules_possible="$aci_modules_possible $1"
          AC_DEFINE([HAVE_$1],1,[Defined if the module is enabled])
          ACTIVE_PACKAGES="$ACTIVE_PACKAGES  $1"
          eval `echo "MODULES_$1=\"$3\""`
          eval `echo "HAVE_$1=yes"`
          ],
          [aci_modules_disabled="$aci_modules_disabled $1"
          ]
          )
])
