def _check(screenImage, template, roi = None, outputMatchResult = False):
        pos = None
        if roi is None or len(roi) == 0:
            search_area = screenImage
        elif len(roi) == 1:
            x, y, w, h = roi[0]
            img_h, img_w = screenImage.shape[:2]
            x_start = max(0, x)
            y_start = max(0, y)
            x_end = min(img_w, x + w)
            y_end = min(img_h, y + h)
            if x_start >= x_end or y_start >= y_end:
                logger.error("错误:roi1范围无效.")
                search_area = screenImage
            else:
                search_area = screenImage[y_start:y_end, x_start:x_end]
        else:
            search_area = CutRoI(screenImage.copy(), roi)
        try:
            result = cv2.matchTemplate(search_area, template, cv2.TM_CCOEFF_NORMED)
        except Exception as e:
                logger.error("{a}".format(a=e))
                logger.info("{a}".format(a=e))
                if isinstance(e, (cv2.error)):
                    logger.info(_("cv2异常."))
                    # SaveImage(screenshot,"cv2异常")
                    return None, 0
                return None, 0

        underscore, max_val, underscore, max_loc = cv2.minMaxLoc(result)

        if outputMatchResult:
            debug_image = search_area.copy()
            SaveImage(debug_image, "origin.png")
            cv2.rectangle(debug_image, max_loc, (max_loc[0] + template.shape[1], max_loc[1] + template.shape[0]), (0, 255, 0), 2)
            SaveImage(debug_image, "matched.png")

        if roi is None or len(roi) == 0:
            pos=[max_loc[0] + template.shape[1]//2,
                 max_loc[1] + template.shape[0]//2]
        else:
            pos=[roi[0][0] + max_loc[0] + template.shape[1]//2,
                 roi[0][1] + max_loc[1] + template.shape[0]//2]
        return pos,max_val
